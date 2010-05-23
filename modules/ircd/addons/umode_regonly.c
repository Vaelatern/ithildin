/*
 * umode_regonly.c: Prevent non-registered users from sending messages
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module prevents non-registered users (tracked via the registered
 * usermode) from sending messages to other users with this flag set.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/umode_reg.h"

IDSTRING(rcsid, "$Id: umode_regonly.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");

/*
@DEPENDENCIES@:        ircd
@DEPENDENCIES@: ircd/addons/umode_reg
*/

static unsigned char usermode_regonly;
HOOK_FUNCTION(can_send_umode_R);

MODULE_LOADER(umode_regonly) {

    if (!get_module_savedata(savelist, "usermode_regonly", &usermode_regonly))
        usermode_request('R', &usermode_regonly, 0, -1, NULL);

    add_hook(ircd.events.can_send_client, can_send_umode_R);

#define ERR_NONONREG 486
    CMSG("486", "You need to identify to a registered nick to "
            "private message %s.");

    return 1;
}

MODULE_UNLOADER(umode_regonly) {
    
    if (reload)
        add_module_savedata(savelist, "usermode_regonly",
                sizeof(usermode_regonly), &usermode_regonly);
    else
        usermode_release(usermode_regonly);

    remove_hook(ircd.events.can_send_client, can_send_umode_R);
    DMSG(ERR_NONONREG);
}

HOOK_FUNCTION(can_send_umode_R) {
    struct client_check_args *ccap = (struct client_check_args *)data;

    if (usermode_isset(ccap->to, usermode_regonly) && !ISREGNICK(ccap->from) &&
            !CLIENT_MASTER(ccap->from)) {
        /* send the error message here, since we will always deny the send, and
         * since it might not be understood otherwise. */
        sendto_one(ccap->from, RPL_FMT(ccap->from, ERR_NONONREG),
                ccap->to->nick);
        return (void *)HOOK_COND_NEVEROK;
    }

    return (void *)HOOK_COND_NEUTRAL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
