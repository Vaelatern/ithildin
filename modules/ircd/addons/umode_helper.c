/*
 * umode_helper.c: Flag mode for users who are designated helpers
 * 
 * Copyright 2003 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module simply adds a flag mode for nicknames to indicate that they are
 * 'designated helpers.'  It's just a little status thing I guess.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/umode_helper.h"
#include "commands/mode.h"
#include "commands/whois.h"

IDSTRING(rcsid, "$Id: umode_helper.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");

/*
@DEPENDENCIES@:        ircd
@DEPENDENCIES@: ircd/commands/mode
@DEPENDENCIES@: ircd/commands/whois
*/

unsigned char usermode_helper;
static unsigned int sflag_helper;

USERMODE_FUNC(helper_usermode_handler);
HOOK_FUNCTION(umode_helper_whois_hook);

MODULE_LOADER(umode_helper) {

    if (!get_module_savedata(savelist, "sflag_helper", &sflag_helper))
        sflag_helper = create_send_flag("HELPER", SEND_LEVEL_OPERATOR, -1);
    if (!get_module_savedata(savelist, "usermode_helper", &usermode_helper)) {
        EXPORT_SYM(helper_usermode_handler);
        usermode_request('h', &usermode_helper, USERMODE_FL_GLOBAL,
                sflag_helper, "helper_usermode_handler");
    }

    add_hook(whois_event, umode_helper_whois_hook);

#define RPL_WHOISHELPER 310
    CMSG("310", "%s :is a network helper");
#define RPL_YOUREHELPER 380
    CMSG("380", ":You are now a helper");
    return 1;
}

MODULE_UNLOADER(umode_helper) {
    
    if (reload) {
        add_module_savedata(savelist, "usermode_helper",
                sizeof(usermode_helper), &usermode_helper);
        add_module_savedata(savelist, "sflag_helper", sizeof(sflag_helper),
                &sflag_helper);
    } else {
        destroy_send_flag(sflag_helper);
        usermode_release(usermode_helper);
    }

    remove_hook(whois_event, umode_helper_whois_hook);

    DMSG(RPL_WHOISHELPER);
    DMSG(RPL_YOUREHELPER);
}

/* Don't let non-operators set themselves +h.  Also don't let anyone but
 * services (or the NULL client) set them +h either.  This means the mode can
 * be set via conventional MODE command methods and also via other commands
 * which invoke MODE in a second-hand fashion. */
USERMODE_FUNC(helper_usermode_handler) {

    if (set && !CLIENT_MASTER(cli) && !OPER(cli) &&
            (by != NULL && !CLIENT_MASTER(by)))
        return 0;

    if (set && MYCLIENT(cli)) /* send the extra numeric too.. */
        sendto_one(cli, RPL_FMT(cli, RPL_YOUREHELPER));

    return 1; /* otherwise, s'alright! */
}

HOOK_FUNCTION(umode_helper_whois_hook) {
    client_t *target = (client_t *)data;
    client_t *cli = cptr.cli;

    if (ISHELPER(target))
        sendto_one(cli, RPL_FMT(cli, RPL_WHOISHELPER), target->nick);

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
