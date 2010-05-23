/*
 * svskill.c: the SVSKILL command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 *
 * The SVSKILL command is, ideally, identical to the KILL command except that
 * it does not do chasing.  How quaint.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: svskill.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

static void do_svskill(client_t *, server_t *, int, char **);

/*
 * kill command.  local opers can kill a comma separated list of users.
 * argv[1] == user(s) to kill
 * argv[2] == kill message
 */
CLIENT_COMMAND(svskill, 2, 2, COMMAND_FL_OPERATOR) {

    if (!CLIENT_MASTER(cli))
        return COMMAND_WEIGHT_NONE; /* newp */
    do_svskill(cli, NULL, argc, argv);

    return COMMAND_WEIGHT_NONE;
}

/* server version, pretty much identical. */
SERVER_COMMAND(svskill, 2, 2, 0) {

    if (!SERVER_MASTER(srv))
        return 0; /* um.. */
    do_svskill(NULL, srv, argc, argv);

    return 0;
}

/* since the client/server versions are nearly identical, we do the work for
 * both of them here. */
static void do_svskill(client_t *cli, server_t *srv, int argc, char **argv) {
    client_t *cp;
    char *msg;
    char reason[TOPICLEN + 1];

    if ((cp = find_client(argv[1])) == NULL)
        return; /* not found.  don't chase. */

    if (argc == 4)
        msg = argv[3];
    else if (argc == 3)
        msg = argv[2];
    else
        msg = "No Reason";

    /* now see if we're offing our client, or somebody else's.. */
    if (MYCLIENT(cp)) {
        snprintf(reason, TOPICLEN, "Killed (%s (%s))",
                (cli != NULL ? cli->nick : srv->name), msg);
        destroy_client(cp, reason);
    } else if (cli_server_uplink(cp) == sptr) {
        /* XXX: I wonder if this will ever even happen.. */
        sendto_flag(ircd.sflag.ops, "Received wrong-direction SVSKILL for %s",
                cp->nick);
    } else
        sendto_one_from(cp, cli, srv, "SVSKILL", ":%s", msg);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
