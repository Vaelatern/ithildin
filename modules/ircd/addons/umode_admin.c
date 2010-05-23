/*
 * umode_admin.c: Flag mode for server admins
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This is just a flag (somewhat vanity) mode to tag users as 'server
 * administrators'
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/umode_admin.h"
#include "commands/whois.h"

IDSTRING(rcsid, "$Id: umode_admin.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");

/*
@DEPENDENCIES@:        ircd
@DEPENDENCIES@: ircd/commands/whois
*/

unsigned char usermode_admin;
static int admin_priv;

USERMODE_FUNC(admin_usermode_handler);
HOOK_FUNCTION(umode_admin_whois_hook);

MODULE_LOADER(umode_admin) {
    uint64_t ui64 = 0;

    if (!get_module_savedata(savelist, "usermode_admin", &usermode_admin)) {
        EXPORT_SYM(admin_usermode_handler);
        usermode_request('A', &usermode_admin, USERMODE_FL_GLOBAL, -1,
                "admin_usermode_handler");
    }
    if (!get_module_savedata(savelist, "admin_priv", &admin_priv))
        admin_priv = create_privilege("administrator", PRIVILEGE_FL_BOOL,
                &ui64, NULL);

    add_hook(whois_event, umode_admin_whois_hook);

#define RPL_WHOISADMIN 308
    CMSG("308", "%s :is an IRC Server Administrator");

    return 1;
}

MODULE_UNLOADER(umode_admin) {
    
    if (reload) {
        add_module_savedata(savelist, "usermode_admin", sizeof(usermode_admin),
                &usermode_admin);
        add_module_savedata(savelist, "admin_priv", sizeof(admin_priv),
                &admin_priv);
    } else {
        destroy_privilege(admin_priv);
        usermode_release(usermode_admin);
    }

    remove_hook(whois_event, umode_admin_whois_hook);

    DMSG(RPL_WHOISADMIN);
}

USERMODE_FUNC(admin_usermode_handler) {
    /* clients may not set the admin flag on themselves without the correct
     * privilege setting. */
    if (by == cli && set && !BPRIV(cli, admin_priv))
        return 0;

    return 1; /* otherwise, s'alright! */
}

HOOK_FUNCTION(umode_admin_whois_hook) {
    client_t *target = (client_t *)data;
    client_t *cli = cptr.cli;

    if (ISADMIN(target))
        sendto_one(cli, RPL_FMT(cli, RPL_WHOISADMIN), target->nick);

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
