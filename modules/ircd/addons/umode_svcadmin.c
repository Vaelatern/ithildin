/*
 * umode_svcadmin.c: Flag mode for server admins
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This is just a flag (somewhat vanity) mode to tag users as 'server
 * administrators'
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/umode_svcadmin.h"
#include "commands/whois.h"

IDSTRING(rcsid, "$Id: umode_svcadmin.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");

/*
@DEPENDENCIES@:        ircd
@DEPENDENCIES@: ircd/commands/whois
*/

unsigned char usermode_svcadmin;
static int svcadmin_priv;

USERMODE_FUNC(svcadmin_usermode_handler);
HOOK_FUNCTION(umode_svcadmin_whois_hook);

MODULE_LOADER(umode_svcadmin) {
    uint64_t ui64 = 0;

    if (!get_module_savedata(savelist, "usermode_svcadmin",
                &usermode_svcadmin)) {
        EXPORT_SYM(svcadmin_usermode_handler);
        usermode_request('a', &usermode_svcadmin, USERMODE_FL_GLOBAL, -1,
                "svcadmin_usermode_handler");
    }
    if (!get_module_savedata(savelist, "svcadmin_priv", &svcadmin_priv))
        svcadmin_priv = create_privilege("services-administrator",
                PRIVILEGE_FL_BOOL, &ui64, NULL);

    add_hook(whois_event, umode_svcadmin_whois_hook);

#define RPL_WHOISSADMIN 309
    CMSG("309", "%s :is a Services Administrator");

    return 1;
}

MODULE_UNLOADER(umode_svcadmin) {
    
    if (reload) {
        add_module_savedata(savelist, "usermode_svcadmin",
                sizeof(usermode_svcadmin), &usermode_svcadmin);
        add_module_savedata(savelist, "svcadmin_priv",
                sizeof(svcadmin_priv), &svcadmin_priv);
    } else {
        destroy_privilege(svcadmin_priv);
        usermode_release(usermode_svcadmin);
    }

    remove_hook(whois_event, umode_svcadmin_whois_hook);

    DMSG(RPL_WHOISSADMIN);
}

USERMODE_FUNC(svcadmin_usermode_handler) {
    /* clients may not set the admin flag on themselves without the correct
     * privilege setting. */
    if (by == cli && set && !BPRIV(cli, svcadmin_priv))
        return 0;

    return 1; /* otherwise, s'alright! */
}

HOOK_FUNCTION(umode_svcadmin_whois_hook) {
    client_t *target = (client_t *)data;
    client_t *cli = cptr.cli;

    if (ISSVCADMIN(target))
        sendto_one(cli, RPL_FMT(cli, RPL_WHOISSADMIN), target->nick);

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
