/*
 * gnotice.c: the GNOTICE command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: gnotice.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

static struct {
    int flag;
    int priv;
} gnotice;

MODULE_LOADER(gnotice) {
    int64_t i64 = 0;

    if (!get_module_savedata(savelist, "gnotice", &gnotice)) {
        gnotice.priv = create_privilege("flag-gnotice",
                PRIVILEGE_FL_BOOL, &i64, NULL);
        gnotice.flag = create_send_flag("GNOTICE", SEND_LEVEL_OPERATOR,
                gnotice.priv);
    }
    
    return 1;
}
MODULE_UNLOADER(gnotice) {

    if (reload)
        add_module_savedata(savelist, "gnotice", sizeof(gnotice), &gnotice);
    else {
        destroy_privilege(gnotice.priv);
        destroy_send_flag(gnotice.flag);
    }
}

/* this command is used to propogate server messages across the network. */
SERVER_COMMAND(gnotice, 0, 1, 0) {

    sendto_serv_butone(sptr, NULL, srv, NULL, "GNOTICE", ":%s", argv[1]);
    sendto_flag_from(gnotice.flag, NULL, srv, "Global", "%s", argv[1]);

    return 0;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
