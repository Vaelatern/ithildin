/*
 * pong.c: the PONG command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: pong.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

/* if a client sends a PONG, just remove the PINGSENT flag */
CLIENT_COMMAND(pong, 1, 2, 0) {

    if (cli->conn != NULL)
        cli->conn->flags &= ~IRCD_CONNFL_PINGSENT;

    return COMMAND_WEIGHT_NONE;
}

/* server PONGs.  may be destined for us or a client.
 * argv[1] == origin
 * argv[2] == destination */
SERVER_COMMAND(pong, 1, 2, 0) {
    char *dest = (argc > 2 ? argv[2] : NULL);
    client_t *dcli;
    server_t *dsrv;

    /* whatever betide, update the 'PINGSENT' status of sptr */
    sptr->conn->flags &= ~IRCD_CONNFL_PINGSENT;

    /* we might be in the middle of establishing, if we aren't, calling
     * server_establish() won't hurt anyhow */
    if (MYSERVER(srv) && !IRCD_SERVER_SYNCHED(srv))
        server_establish(srv);

    /* attempt to route the PONG message */
    if (dest != NULL) {
        if ((dcli = find_client(dest)) || (dsrv = find_server(dest))) {
            if (dcli != NULL)
                sendto_one_target(dcli, NULL, cptr.srv, NULL, "PONG",
                        "%s :%s", argv[1], argv[2]);
            else if (dsrv != NULL && dsrv != ircd.me)
                sendto_serv_from(dsrv, NULL, cptr.srv, NULL, "PONG",
                        "%s :%s", argv[1], argv[2]);
        }
    }

    return 0;
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
