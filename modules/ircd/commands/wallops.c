/*
 * wallops.c: the WALLOPS command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: wallops.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

static struct {
    int flag;
    unsigned char umode;
} wallops;

MODULE_LOADER(wallops) {

    if (!get_module_savedata(savelist, "wallops", &wallops)) {
        /* ask for a +w mode and also create a WALLOPS send flag */
        wallops.flag = create_send_flag("WALLOPS", 0, -1);
        usermode_request('w', &wallops.umode, 0, wallops.flag, NULL);
    }

    return 1;
}
MODULE_UNLOADER(wallops) {
    
    if (reload)
        add_module_savedata(savelist, "wallops", sizeof(wallops), &wallops);
    else {
        usermode_release(wallops.umode);
        destroy_send_flag(wallops.flag);
    }
}

/* the wallops command.  sends wallops to all users who are +w/in the 'WALLOPS'
 * send group. */
CLIENT_COMMAND(wallops, 1, 1, COMMAND_FL_OPERATOR) {

    /* send it off to all LOCAL +w users */
    sendto_group(&ircd.sflag.flags[wallops.flag].users, 0, cli, NULL,
            "WALLOPS", ":%s", argv[1]);
    sendto_serv_butone(sptr, cli, NULL, NULL, "WALLOPS", ":%s", argv[1]);
    return COMMAND_WEIGHT_NONE;
}

SERVER_COMMAND(wallops, 1, 1, 0) {

    /* send it off to all LOCAL +w users */
    sendto_group(&ircd.sflag.flags[wallops.flag].users, 0, NULL, srv,
            "WALLOPS", ":%s", argv[1]);
    sendto_serv_butone(sptr, NULL, srv, NULL, "WALLOPS", ":%s", argv[1]);
    return 0; 
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
