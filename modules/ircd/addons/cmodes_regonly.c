/*
 * cmodes_regonly.c: Registered nick channel restrictions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * These two modes restrict non-registered users from joining in or speaking in
 * a channel.  They work with the 'regnicks' addon.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"
#include "addons/umode_reg.h"
#include "commands/mode.h"

IDSTRING(rcsid, "$Id: cmodes_regonly.c 613 2005-11-22 13:43:19Z wd $");

MODULE_REGISTER("$Rev: 613 $");

/*
@DEPENDENCIES@:        ircd
@DEPENDENCIES@: ircd/addons/core
@DEPENDENCIES@: ircd/addons/umode_reg
*/

static struct {
    unsigned char M;
    unsigned char R;
} regonly_chanmodes;
    
HOOK_FUNCTION(can_send_cmode_M);
HOOK_FUNCTION(can_join_cmode_R);

MODULE_LOADER(cmodes_regonly) {

    if (!get_module_savedata(savelist, "regonly_chanmodes",
                &regonly_chanmodes)) {
        chanmode_request('M', &regonly_chanmodes.M, CHANMODE_FL_D,
                "chanmode_flag", "chanmode_flag_query", 0, NULL);
        chanmode_request('R', &regonly_chanmodes.R, CHANMODE_FL_D,
                "chanmode_flag", "chanmode_flag_query", 0, NULL);
    }
    add_hook(ircd.events.can_send_channel, can_send_cmode_M);
    add_hook(ircd.events.can_join_channel, can_join_cmode_R);

#define ERR_NEEDREGGEDNICK 477
    CMSG("477", "%s :You need to identify to a registered nickname to %s "
            "that channel.");
#define ERR_NONONREG 486
    CMSG("486", "You need to identify to a registered nick to "
            "private message %s.");
    return 1;
}

MODULE_UNLOADER(cmodes_regonly) {
    
    if (reload)
        add_module_savedata(savelist, "regonly_chanmodes",
                sizeof(regonly_chanmodes), &regonly_chanmodes);
    else {
        chanmode_release(regonly_chanmodes.M);
        chanmode_release(regonly_chanmodes.R);
    }

    remove_hook(ircd.events.can_send_channel, can_send_cmode_M);
    remove_hook(ircd.events.can_join_channel, can_join_cmode_R);

    DMSG(ERR_NEEDREGGEDNICK);
    DMSG(ERR_NONONREG);
}

/* A simple function to check channel access. */
HOOK_FUNCTION(can_send_cmode_M) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;

    /* do the +M check.  this only checks for a non-registered nick since the
     * operator check function should always be ahead of this (since the 'core'
     * module is a dependent and is loaded first. */
    if (chanmode_isset(ccap->chan, regonly_chanmodes.M) &&
            !ISREGNICK(ccap->cli)) {
        /* send the error ourselves, and return NEVEROK */
        sendto_one(ccap->cli, RPL_FMT(ccap->cli, ERR_NEEDREGGEDNICK),
                ccap->chan->name, "send to");
        return (void *)HOOK_COND_NEVEROK; /* bleah. */
    }

    return (void *)HOOK_COND_NEUTRAL;
}

HOOK_FUNCTION(can_join_cmode_R) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;

    if (chanmode_isset(ccap->chan, regonly_chanmodes.R) &&
            !ISREGNICK(ccap->cli)) {
        /* send them the error and return. */
        sendto_one(ccap->cli, RPL_FMT(ccap->cli, ERR_NEEDREGGEDNICK),
                ccap->chan->name, "join");
        return (void *)HOOK_COND_NEVEROK;
    }
    return (void *)HOOK_COND_NEUTRAL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
