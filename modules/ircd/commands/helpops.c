/*
 * helpops.c: the HELPOPS command
 * 
 * Copyright 2003 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/umode_helper.h"

IDSTRING(rcsid, "$Id: helpops.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/umode_helper
*/

/* The 'HELPOPS' command.  This allows +h users to send messages to each other
 * much like chatops or globops messages.  I think this is of dubious value,
 * but hey. */
CLIENT_COMMAND(helpops, 1, 1, 0) {

    if (!ISHELPER(cli)) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOPRIVILEGES));
        return COMMAND_WEIGHT_NONE;
    }

    /* send it off .. */
    sendto_flag_from(SFLAG("HELPER"), cli, NULL, "HelpOps", "%s", argv[1]);
    sendto_serv_butone(sptr, cli, NULL, NULL, "HELPOPS", ":%s", argv[1]);

    return COMMAND_WEIGHT_LOW;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
