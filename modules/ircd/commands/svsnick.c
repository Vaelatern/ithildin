/*
 * svsnick.c: the SVSNICK command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: svsnick.c 593 2005-10-10 07:51:31Z wd $");

MODULE_REGISTER("$Rev: 593 $");
/*
@DEPENDENCIES@: ircd
*/

static void do_svsnick(client_t *, server_t *, int, char **);

CLIENT_COMMAND(svsnick, 3, 3, 0) {

    if (!CLIENT_MASTER(cli))
        return COMMAND_WEIGHT_NONE; /* hmpf */

    do_svsnick(cli, NULL, argc, argv);

    return COMMAND_WEIGHT_NONE;
}
SERVER_COMMAND(svsnick, 3, 3, 0) {

    if (!SERVER_MASTER(srv))
        return 0; /* hmpf */

    do_svsnick(NULL, srv, argc, argv);

    return 0;
}

/* argv[1] == old nick, argv[2] == new nick, argv[3] == timestamp */
static void do_svsnick(client_t *cli, server_t *srv, int argc, char **argv) {
    client_t *cp;

    if ((cp = find_client(argv[2])) != NULL) {
        /* if the new nickname is already in use, go ahead and send a collide
         * in both directions.  ptooey. */
        sendto_serv_butone(sptr, NULL, sptr, cp->nick, "KILL",
                ":%s (SVSNICK Collide)", sptr->name);
        sendto_one_from(cp, NULL, sptr, "KILL", ":%s (SVSNICK Collide)",
                sptr->name);
        return;
    }

    /* if it goes to us, let us make with the nickname change. */
    if (pass_command(cli, srv, "SVSNICK", "%s %s :%s", argc, argv, 1) ==
            COMMAND_PASS_LOCAL) {
        cp = find_client(argv[1]);
        /* cp should never be NULL, pass_command will gaurantee that */
        sendto_common_channels(cp, NULL, "NICK", ":%s", argv[2]);
        sendto_serv_butone(NULL, cp, NULL, NULL, "NICK", "%s :%d", argv[2],
                cp->ts);
        client_change_nick(cp, argv[2]);
    }
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
