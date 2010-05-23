/*
 * locops.c: the LOCOPS command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: locops.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

static struct {
    int flag;
    int priv;
} locops;

MODULE_LOADER(locops) {
    int64_t i64 = 1;

    if (!get_module_savedata(savelist, "locops", &locops)) {
        locops.priv = create_privilege("flag-locops", PRIVILEGE_FL_BOOL, &i64,
                NULL);
        locops.flag = create_send_flag("LOCOPS", SEND_LEVEL_OPERATOR,
                locops.priv);
    }

    return 1;
}
MODULE_UNLOADER(locops) {
    
    if (reload)
        add_module_savedata(savelist, "locops", sizeof(locops), &locops);
    else {
        destroy_privilege(locops.priv);
        destroy_send_flag(locops.flag);
    }
}

/* the locops command.  sends the message to all local operators in the
 * 'LOCOPS' group. */
CLIENT_COMMAND(locops, 1, 1, COMMAND_FL_OPERATOR) {

    /* send it off .. */
    sendto_flag_from(locops.flag, cli, NULL, "LocOps", "%s", argv[1]);
    return COMMAND_WEIGHT_NONE;
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
