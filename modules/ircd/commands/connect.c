/*
 * connect.c: the CONNECT command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: connect.c 827 2008-11-20 18:06:33Z wd $");

MODULE_REGISTER("$Rev: 827 $");
/*
@DEPENDENCIES@: ircd
*/

struct privilege_tuple priv_connect_tuple[] = {
#define CONNECT_LOCAL 0
    { "local",        CONNECT_LOCAL },
#define CONNECT_REMOTE 1
    { "remote",        CONNECT_REMOTE },
    { NULL,        0 }
};
int priv_connect;

MODULE_LOADER(connect) {
    int64_t i64 = CONNECT_LOCAL;

    priv_connect = create_privilege("connect", PRIVILEGE_FL_TUPLE, &i64,
            &priv_connect_tuple);
    return 1;
}
MODULE_UNLOADER(connect) {

    destroy_privilege(priv_connect);
}

/* the connect command:
 * argv[1] == server to connect
 * argv[2] ?= port to connect it on
 * argv[3] ?= server to connect it to (if remote) */
CLIENT_COMMAND(connect, 1, 3, COMMAND_FL_OPERATOR) {
    struct server_connect *scp;
    server_t *sp;
    char msg[512];
    
    /* well, this isn't as simple as doing 'pass_command' since we need to
     * determine if they are allowed to connect remote servers at all. */
    if (MYCLIENT(cli) && argc > 3 && find_server(argv[3]) != ircd.me &&
                TPRIV(cli, priv_connect) != CONNECT_REMOTE) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOPRIVILEGES));
        return COMMAND_WEIGHT_NONE;
    }

    /* otherwise .. */
    if (pass_command(cli, NULL, "CONNECT", "%s %s %s", argc, argv, 3) !=
            COMMAND_PASS_LOCAL)
        return COMMAND_WEIGHT_NONE; /* sent along ... */

    if ((sp = find_server(argv[1])) != NULL) {
        sendto_one(cli, "NOTICE", ":Connect: server %s already exists from %s",
                sp->name,
                (MYSERVER(sp) || sp == ircd.me ? ircd.me->name : sp->parent->name));
        return COMMAND_WEIGHT_NONE; /* server already on the network, foo. */
    }

    /* okay, so it's us.  find the server_connect entry and see about issuing a
     * connect. :) */
    if ((scp = find_server_connect(argv[1])) == NULL) {
        sendto_one(cli, "NOTICE", ":Connect: No server section found for %s.",
                argv[1]);
        return COMMAND_WEIGHT_NONE;
    }

    sprintf(msg, "%s CONNECT %s%s%s from %s",
            MYCLIENT(cli) ? "Local" : "Remote", argv[1], (argc > 2 ? " " : ""),
            (argc > 2 ? argv[2] : ""), cli->nick);
    sendto_flag(SFLAG("GNOTICE"), "%s", msg);
    sendto_serv_butone(NULL, NULL, ircd.me, NULL, "GNOTICE", ":%s", msg);
    switch (server_connect(scp, (argc > 2 ? argv[2] : NULL))) {
        case 0:
            sendto_one(cli, "NOTICE", ":*** Connection to %s failed.",
                    scp->name);
            break;
        default:
            sendto_one(cli, "NOTICE", ":*** Connecting to %s.", scp->name);
            break;
    }

    return COMMAND_WEIGHT_NONE;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
