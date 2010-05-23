/*
 * chatops.c: the CHATOPS command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 *
 * I would like to note my personal objection to this command.  I've written
 * and included only to provide backwards compatibility, and if you can
 * discourage people from using it, you absolutely should.  This functionality
 * is supplied completely in channels, and the idea of having a command simply
 * for operators to gab at each other is repugnant.  Death to chatops.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: chatops.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

static struct {
    int priv;
    int flag;
    unsigned char mode;
} chatops;

MODULE_LOADER(chatops) {
    int64_t i64 = 1;

    if (!get_module_savedata(savelist, "chatops", &chatops)) {
        chatops.priv = create_privilege("flag-chatops", PRIVILEGE_FL_BOOL, &i64,
                NULL);
        /* I'm restoring the old behavior of this command, which DALnet nuked
         * for some odd reason.  If you set +c/enter the chatops group, you
         * must be an operator, but you can remain in the group even if you
         * deoper at a later point. */
        chatops.flag = create_send_flag("CHATOPS",
                SEND_LEVEL_OPERATOR|SEND_LEVEL_PRESERVE, chatops.priv);
        chatops.mode = usermode_request('b', &chatops.mode,
                USERMODE_FL_OPER|USERMODE_FL_PRESERVE, chatops.flag,
                NULL);
    }

    return 1;
}
MODULE_UNLOADER(chatops) {
    
    if (reload)
        add_module_savedata(savelist, "chatops", sizeof(chatops), &chatops);
    else {
        usermode_release(chatops.mode);
        destroy_send_flag(chatops.flag);
        destroy_privilege(chatops.priv);
    }
}

/* the chatops command.  sends the message to all operators who are +c/in the
 * CHATOPS group. */
CLIENT_COMMAND(chatops, 1, 1, 0) {

    /* if they're not in the chatops group, don't let them send the command. */
    if (!in_send_flag(chatops.flag, cli)) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOPRIVILEGES));
        return 0;
    }

    /* send it off .. */
    sendto_flag_from(chatops.flag, cli, NULL, "ChatOps", "%s", argv[1]);
    sendto_serv_butone(sptr, cli, NULL, NULL, "CHATOPS", ":%s", argv[1]);
    return COMMAND_WEIGHT_LOW;
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
