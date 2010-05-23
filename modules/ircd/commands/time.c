/*
 * time.c: the TIME command.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: time.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

MODULE_LOADER(time) {

#define RPL_TIME 391
    CMSG("391", "%s :%s");

    return 1;
}
MODULE_UNLOADER(time) {

    DMSG(RPL_TIME);
}

/* argv[1] ?= server to query */
CLIENT_COMMAND(time, 0, 1, 0) {
    char tstr[80];

    if (pass_command(cli, NULL, "TIME", ":%s", argc, argv, 1) !=
            COMMAND_PASS_LOCAL)
        return COMMAND_WEIGHT_MEDIUM;

    /* send as Day Mon dd HH:MM:SS TZ yyyy */
    strftime(tstr, 80, "%a %b %d %H:%M:%S %Z %Y", localtime(&me.now));
    sendto_one(cli, RPL_FMT(cli, RPL_TIME), ircd.me->name, tstr);

    return COMMAND_WEIGHT_LOW;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
