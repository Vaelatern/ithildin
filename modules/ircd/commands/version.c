/*
 * version.c: the VERSION command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: version.c 748 2006-06-03 23:25:00Z wd $");

MODULE_REGISTER("$Rev: 748 $");
/*
@DEPENDENCIES@: ircd
*/

MODULE_LOADER(version) {

    /* now create numerics */
#define RPL_VERSION 351
    CMSG("351", "%s %s :%s");

    return 1;
}
MODULE_UNLOADER(version) {

    DMSG(RPL_VERSION);
}

/* argv[1] = server to request from */
CLIENT_COMMAND(version, 0, 1, COMMAND_FL_REGISTERED) {

    if (pass_command(cli, NULL, "VERSION", "%s", argc, argv, 1) !=
            COMMAND_PASS_LOCAL)
        return COMMAND_WEIGHT_HIGH;

    sendto_one(cli, RPL_FMT(cli, RPL_VERSION), ircd.version, ircd.me->name,
            ircd.vercomment);
    send_isupport(cli);
    if (me.debug)
        sendto_one(cli, "NOTICE", ":This server is running in debug mode. "
                "Some traffic may be logged.");

    return COMMAND_WEIGHT_MEDIUM;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
