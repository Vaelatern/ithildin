/*
 * globops.c: the GLOBOPS command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: globops.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

static struct {
    int priv;
    int flag;
    unsigned char mode;
} globops;

MODULE_LOADER(globops) {
    int64_t i64 = 1;

    if (!get_module_savedata(savelist, "globops", &globops)) {
        globops.priv = create_privilege("flag-globops", PRIVILEGE_FL_BOOL, &i64,
                NULL);
        globops.flag = create_send_flag("GLOBOPS", SEND_LEVEL_OPERATOR,
                globops.priv);
        globops.mode = usermode_request('g', &globops.mode, USERMODE_FL_OPER,
                globops.flag, NULL);
    }

    return 1;
}
MODULE_UNLOADER(globops) {
    
    if (reload)
        add_module_savedata(savelist, "globops", sizeof(globops), &globops);
    else {
        usermode_release(globops.mode);
        destroy_send_flag(globops.flag);
        destroy_privilege(globops.priv);
    }
}

/* the globops command.  sends the message to all operators who are +g/in the
 * GLOBOPS group. */
CLIENT_COMMAND(globops, 1, 1, COMMAND_FL_OPERATOR) {

    /* send it off .. */
    sendto_flag_from(globops.flag, cli, NULL, "Global", "%s", argv[1]);
    sendto_serv_butone(sptr, cli, NULL, NULL, "GLOBOPS", ":%s", argv[1]);
    return COMMAND_WEIGHT_NONE;
}

/* just like the client command, but for servers. */
SERVER_COMMAND(globops, 1, 1, 0) {

    /* send it off .. */
    sendto_flag_from(globops.flag, NULL, srv, "Global", "%s", argv[1]);
    sendto_serv_butone(sptr, NULL, srv, NULL, "GLOBOPS", ":%s", argv[1]);
    return 0;
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
