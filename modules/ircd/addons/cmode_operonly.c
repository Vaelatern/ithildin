/*
 * cmode_operonly.c: Operator-only access limiter for channels.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This mode provides a way to restrict channel entry to operators only.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"

IDSTRING(rcsid, "$Id: cmode_operonly.c 613 2005-11-22 13:43:19Z wd $");

MODULE_REGISTER("$Rev: 613 $");

/*
@DEPENDENCIES@:        ircd
@DEPENDENCIES@: ircd/addons/core
*/

static unsigned char chanmode_operonly;

HOOK_FUNCTION(can_join_cmode_O);

MODULE_LOADER(cmode_operonly) {

    if (!get_module_savedata(savelist, "chanmode_operonly",
                &chanmode_operonly))
        chanmode_request('O', &chanmode_operonly, CHANMODE_FL_D,
                "chanmode_flag", "chanmode_flag_query", 0, NULL);

    add_hook(ircd.events.can_join_channel, can_join_cmode_O);

    return 1;
}

MODULE_UNLOADER(cmode_operonly) {
    
    if (reload)
        add_module_savedata(savelist, "chanmode_operonly",
                sizeof(chanmode_operonly), &chanmode_operonly);
    else
        chanmode_release(chanmode_operonly);

    remove_hook(ircd.events.can_join_channel, can_join_cmode_O);
}

HOOK_FUNCTION(can_join_cmode_O) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;

    if (chanmode_isset(ccap->chan, chanmode_operonly) && !OPER(ccap->cli)) {
        /* send them the error and return. */
        sendto_one(ccap->cli, RPL_FMT(ccap->cli, ERR_NOPRIVILEGES));
        return (void *)HOOK_COND_NEVEROK;
    }

    return (void *)HOOK_COND_NEUTRAL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
