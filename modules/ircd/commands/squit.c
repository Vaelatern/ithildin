/*
 * squit.c: the SQUIT command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: squit.c 718 2006-04-25 11:44:28Z wd $");

MODULE_REGISTER("$Rev: 718 $");
/*
@DEPENDENCIES@: ircd
*/

struct privilege_tuple priv_squit_tuple[] = {
#define SQUIT_LOCAL 0
    { "local",        SQUIT_LOCAL },
#define SQUIT_REMOTE 1
    { "remote",        SQUIT_REMOTE },
    { NULL,        0 }
};
int priv_squit;

MODULE_LOADER(squit) {
    int64_t i64 = SQUIT_LOCAL;

    priv_squit = create_privilege("squit", PRIVILEGE_FL_TUPLE, &i64,
            &priv_squit_tuple);
    return 1;
}
MODULE_UNLOADER(squit) {

    destroy_privilege(priv_squit);
}

CLIENT_COMMAND(squit, 1, 2, COMMAND_FL_OPERATOR) {
    server_t *sp;
    char *msg = (argc > 2 ? argv[2] : cli->nick);

    if ((sp = find_server(argv[1])) != NULL) {
        /* if this is our client, examine their behavior rather skeptically.. */
        if (MYCLIENT(cli)) {
            client_t *cp = find_client(argv[1]);

            if (sp != NULL && !MYSERVER(sp) &&
                    TPRIV(cli, priv_squit) != SQUIT_REMOTE) {
                sendto_one(cli, RPL_FMT(cli, ERR_NOPRIVILEGES));
                return COMMAND_WEIGHT_NONE;
            }
            if (cp == cli || sp == ircd.me) {
                /* preserve this old behavior. ;) I find it amusing. */
                destroy_client(cli, argv[2]);
                return IRCD_CONNECTION_CLOSED;
            }

            sendto_flag(SFLAG("GNOTICE"), "Received SQUIT %s from %s (%s)",
                    sp->name, cli->nick, msg);
        }

        /* If sp is our server, we're definitely going to kick it off now,
         * so do that.  destroy_server will happily send out the SQUITs for
         * us. */
        if (MYSERVER(sp)) {
            if (!MYCLIENT(cli)) {
                sendto_serv_butone(NULL, NULL, ircd.me, NULL, "GNOTICE",
                        ":Remote SQUIT %s from %s (%s)", sp->name, cli->nick,
                        msg);
                sendto_flag(SFLAG("GNOTICE"), "Remote SQUIT %s from %s (%s)",
                        sp->name, cli->nick, msg);
            }
        } else
            /* We are an interrim along the road, ensure the final
             * destination gets the message. */
            sendto_serv_from(sp, cli, NULL, sp->name, "SQUIT", ":%s", msg);

        destroy_server(sp, msg);
    } else
        sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHSERVER), argv[1]);

    return COMMAND_WEIGHT_NONE;
}

SERVER_COMMAND(squit, 0, 2, 0) {
    server_t *sp;
    char *msg = (argc > 2 ? argv[2] : srv->name);

    /* mimicking other ircd behavior here .. if we get a SQUIT with no
     * parameters we lovingly drop the sender on his head. */
    if (argc < 2) {
        destroy_server(srv, msg);
        return IRCD_CONNECTION_CLOSED;
    }

    if ((sp = find_server(argv[1])) == NULL) {
        log_debug("received SQUIT for unknown server %s from %s", argv[1],
                srv->name);
        return 0;
    }

    /* and if it's us.. we do nothing (anticipating that uplink will deal
     * with things appropriately.) */
    if (sp == ircd.me)
        return 0;

    /* if this is our server, send notice and close their connection, if not,
     * keep sending towards that server. */
    if (MYSERVER(sp)) {
        sendto_flag(SFLAG("GNOTICE"), "Remote SQUIT %s from %s (%s)",
                sp->name, srv->name, msg);
        sendto_serv_butone(NULL, NULL, ircd.me, NULL, "GNOTICE",
                ":Remote SQUIT %s from %s (%s)", sp->name, srv->name, msg);
    } else
        /* Otherwise we must continue to pass the SQUIT towards its final
         * destination, as well as destroying the server.  See the
         * commentary in destroy_server for an explanation of what messages
         * this sends. */
        sendto_serv_from(sp, NULL, srv, sp->name, "SQUIT", ":%s", msg);

    destroy_server(sp, msg);

    if (sp == srv)
        /* If the command came from the server we're killing, be sure to
         * send back a 'CLOSEDCONN' so we stop parsing the buffer. */
        return IRCD_CONNECTION_CLOSED;
    else
        return 0;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
