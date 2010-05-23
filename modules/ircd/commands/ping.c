/*
 * ping.c: the PING command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: ping.c 811 2007-09-04 03:29:58Z wd $");

MODULE_REGISTER("$Rev: 811 $");
/*
@DEPENDENCIES@: ircd
*/

/* argv[1]: origin
 * argv[2]: destination  
 * in the client version of PING, origin is ignored because we don't trust
 * clients.  If the destination is a remote server, route the message to it,
 * otherwise, just return a PONG with our servername */
CLIENT_COMMAND(ping, 1, 2, 0) {
    server_t *sp;

    if (argc < 3)
        sp = ircd.me;
    else if ((sp = find_server(argv[2])) == NULL) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHSERVER), argv[2]);
        return COMMAND_WEIGHT_MEDIUM;
    }

    /* sp is now set to our destination.  cli is our origin.
     * Note that in the first case we return whatever the client sends in
     * argv[1] since some clients rely on this behavior. */
    if (sp == ircd.me)
        sendto_one_target(cli, NULL, ircd.me, NULL, "PONG", "%s :%s",
                ircd.me->name, argv[1]);
    else
        sendto_serv_from(sp, cli, NULL, NULL, "PING", "%s :%s", cli->nick,
                sp->name);

    return COMMAND_WEIGHT_LOW;
}

/* server ping requests are basically like client ping requests */
SERVER_COMMAND(ping, 1, 2, 0) {
    server_t *sp;

    if (argc < 3)
        sp = ircd.me;
    else if ((sp = find_server(argv[2])) != NULL)
        return 0;

    /* sp is now set to our destination.  srv is our origin. */
    if (sp == ircd.me)
        sendto_serv_from(srv, NULL, ircd.me, NULL, "PONG", "%s :%s",
                ircd.me->name, srv->name);
    else
        sendto_serv_from(sp, NULL, srv, NULL, "PING", "%s :%s", srv->name,
                sp->name);

    return 0;
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
