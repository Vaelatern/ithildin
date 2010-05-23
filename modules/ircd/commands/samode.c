/*
 * samode.c: the SAMODE command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/umode_svcadmin.h"
#include "commands/mode.h"

IDSTRING(rcsid, "$Id: samode.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/umode_svcadmin
@DEPENDENCIES@: ircd/commands/mode
*/

CLIENT_COMMAND(samode, 2, 0, COMMAND_FL_OPERATOR) {
    channel_t *chan;
    char buf[512];
    int len;
    int oarg = 2;

    if (!ISSVCADMIN(cli)) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOPRIVILEGES));
        return COMMAND_WEIGHT_NONE;
    }

    if ((chan = find_channel(argv[1])) == NULL) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHCHANNEL), argv[1]);
        return COMMAND_WEIGHT_NONE;
    }

    len = snprintf(buf, 512, "%s used SAMODE (%s", cli->nick, chan->name);
    while (argc > oarg)
        len += snprintf(buf + len, 512 - len, " %s", argv[oarg++]);
    snprintf(buf + len, 512 - len, ")");

    sendto_serv_butone(NULL, NULL, ircd.me, NULL, "GLOBOPS", ":%s", buf);
    sendto_flag(SFLAG("GLOBOPS"), "%s", buf);
    channel_mode(NULL, ircd.me, chan, chan->created, argc - 2, argv + 2, 1);

    return COMMAND_WEIGHT_NONE;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
