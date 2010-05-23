/*
 * cmode_private.c: Flag mode for 'private' channels.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This is just a dummy module for networks which still want to use the +p mode
 * in the rfc1459(ish) way.  +p is basically a duplicate of +s, and really
 * there's no reason to use it except to satisfy legacy
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"

IDSTRING(rcsid, "$Id: cmode_reg.c 502 2003-06-26 13:50:55Z wd $");

MODULE_REGISTER("$Rev: 502 $");

/*
@DEPENDENCIES@:        ircd ircd/addons/core
*/

static unsigned char chanmode_private;
static HOOK_FUNCTION(can_show_chan_private);

MODULE_LOADER(cmode_private) {

    if (!get_module_savedata(savelist, "chanmode_private",
                &chanmode_private))
        chanmode_request('p', &chanmode_private, CHANMODE_FL_D,
                "chanmode_flag", "chanmode_flag_query", 0, NULL);

    add_hook(ircd.events.can_see_channel, can_show_chan_private);

    return 1;
}

MODULE_UNLOADER(cmode_private) {
    
    remove_hook(ircd.events.can_see_channel, can_show_chan_private);

    if (reload)
        add_module_savedata(savelist, "chanmode_private",
                sizeof(chanmode_private), &chanmode_private);
    else
        chanmode_release(chanmode_private);
}

HOOK_FUNCTION(can_show_chan_private) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;

    if (ccap->clp != NULL)
        return (void *)HOOK_COND_OK;
    else if (chanmode_isset(ccap->chan, chanmode_private)) {
        if (OPER(ccap->cli) && BPRIV(ccap->cli, core.privs.see_hidden_chan))
            return (void *)HOOK_COND_ALWAYSOK; /* okay, but sketchy.  */
        else
            return (void *)ERR_NOTONCHANNEL;
    } else
        return (void *)HOOK_COND_OK; /* it's not +s, okay by us. */
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
