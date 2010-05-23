/*
 * umode_reg.c: Flag mode for registered nicknames.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module simply adds a flag mode for nicknames to indicate that they are
 * registered.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/umode_reg.h"
#include "commands/mode.h"
#include "commands/whois.h"

IDSTRING(rcsid, "$Id: umode_reg.c 619 2005-11-22 18:40:33Z wd $");

MODULE_REGISTER("$Rev: 619 $");

/*
@DEPENDENCIES@:        ircd
@DEPENDENCIES@: ircd/commands/mode
@DEPENDENCIES@: ircd/commands/whois
*/

unsigned char reg_umode;

USERMODE_FUNC(usermode_reg);
HOOK_FUNCTION(umode_reg_nick_hook);
HOOK_FUNCTION(umode_reg_whois_hook);

MODULE_LOADER(umode_reg) {

    if (!get_module_savedata(savelist, "reg_umode", &reg_umode)) {
        EXPORT_SYM(usermode_reg);
        usermode_request('r', &reg_umode, USERMODE_FL_GLOBAL, -1,
                "usermode_reg");
    }

    add_hook(ircd.events.client_nick, umode_reg_nick_hook);
    add_hook(whois_event, umode_reg_whois_hook);

#define RPL_WHOISREGNICK 307
    CMSG("307", "%s :has identified for this nick");

    return 1;
}

MODULE_UNLOADER(umode_reg) {
    
    if (reload)
        add_module_savedata(savelist, "reg_umode", sizeof(reg_umode),
                &reg_umode);
    else
        usermode_release(reg_umode);

    remove_hook(ircd.events.client_nick, umode_reg_nick_hook);
    remove_hook(whois_event, umode_reg_whois_hook);

    DMSG(RPL_WHOISREGNICK);
}

/* Don't let users change their registered/non-registered setting. */
USERMODE_FUNC(usermode_reg) {
    /* clients cannot modify the +r mode themselves.  note however that
     * this will *not* cause the mode to fail for remote clients
     * because of the test in usermode_set() so a local client check
     * here is unnecessary. */
    if (by == cli && !CLIENT_MASTER(cli))
        return 0;

    return 1; /* otherwise, s'alright! */
}

HOOK_FUNCTION(umode_reg_nick_hook) {
    client_t *cli = (client_t *)data;
    char *fakeargv[1];
    char xxx[2]; /* XXX: bad hack :) */

    if (ISREGNICK(cli)) {
        fakeargv[0] = xxx;
        xxx[0] = reg_umode;
        xxx[1] = '\0';
        user_mode(cli, cli, 1, fakeargv, 1);
    }

    return HOOK_COND_NEUTRAL;
}

HOOK_FUNCTION(umode_reg_whois_hook) {
    client_t *target = (client_t *)data;
    client_t *cli = cptr.cli;

    if (ISREGNICK(target))
        sendto_one(cli, RPL_FMT(cli, RPL_WHOISREGNICK), target->nick);

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
