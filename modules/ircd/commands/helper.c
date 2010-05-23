/*
 * helper.c: the HELPER command
 * 
 * Copyright 2003 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/umode_helper.h"
#include "commands/mode.h"

IDSTRING(rcsid, "$Id: helper.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/umode_helper
@DEPENDENCIES@: ircd/commands/mode
*/

/* the 'helper' command:
 * argv[1] == user to give 'helper' status to */
CLIENT_COMMAND(helper, 1, 1, COMMAND_FL_OPERATOR) {
    client_t *cp;
    char *fargv[2];
    char msg[512];
    
    /* if the user is not local pass the command to the user's server */
    if (pass_command(cli, NULL, "HELPER", "%s", argc, argv, 1) !=
            COMMAND_PASS_LOCAL)
        return COMMAND_WEIGHT_NONE; /* sent along ... */

    cp = find_client(argv[1]); /* should never be NULL now */
    /* otherwise it's our user.  send a notice letting people know what was
     * done, and then pass around the +h mode setting */
    sprintf(msg, "%s has given helper status to %s", cli->nick, cp->nick);
    sendto_flag(SFLAG("GLOBOPS"), "%s", msg);
    sendto_serv_butone(sptr, NULL, ircd.me, NULL, "GLOBOPS", ":%s", msg);

    /* now set the fake arguments and send the mode.  yuck :) */
    msg[0] = '+';
    msg[1] = usermode_helper;
    msg[2] = '\0';
    fargv[0] = msg;
    fargv[1] = NULL;
    user_mode(NULL, cp, 1, fargv, 1);

    return COMMAND_WEIGHT_NONE;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
