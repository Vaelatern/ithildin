/*
 * cmode_reg.c: Flag mode for registered channels.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module simply adds a flag mode for channels to indicate that they are
 * registered.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/cmode_reg.h"

IDSTRING(rcsid, "$Id: cmode_reg.c 619 2005-11-22 18:40:33Z wd $");

MODULE_REGISTER("$Rev: 619 $");

/*
@DEPENDENCIES@:        ircd
*/

unsigned char reg_cmode;

CHANMODE_FUNC(chanmode_reg);

MODULE_LOADER(cmode_reg) {

    if (!get_module_savedata(savelist, "reg_cmode", &reg_cmode)) {
        EXPORT_SYM(chanmode_reg);
        chanmode_request('r', &reg_cmode, CHANMODE_FL_D,
                "chanmode_reg", "chanmode_flag_query", 0, NULL);
    }

#define ERR_ONLYSERVERSCANCHANGE 468
    CMSG("468", "%s :Only servers can change that mode.");

    return 1;
}

MODULE_UNLOADER(cmode_reg) {
    
    if (reload)
        add_module_savedata(savelist, "reg_cmode", sizeof(reg_cmode),
                &reg_cmode);
    else
        chanmode_release(reg_cmode);

    DMSG(ERR_ONLYSERVERSCANCHANGE);
}

/* Handle the 'r' channel mode.  Make sure the right folks are the only ones
 * who can set it. */
CHANMODE_FUNC(chanmode_reg) {

    *argused = 0;
    if (cli != NULL && !CLIENT_MASTER(cli)) {
        sendto_one(cli, RPL_FMT(cli, ERR_ONLYSERVERSCANCHANGE), chan->name);
        return CHANMODE_FAIL; /* nyet comrade */
    }
    
    switch (set) {
        case CHANMODE_SET:
            chanmode_setflag(chan, mode);
            break;
            /* fallthrough to the unset case */
        case CHANMODE_UNSET:
            chanmode_unsetflag(chan, mode);
            break;
    }

    return CHANMODE_OK;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
