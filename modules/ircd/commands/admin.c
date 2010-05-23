/*
 * admin.c: the ADMIN command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: admin.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

MODULE_LOADER(admin) {

    /* now create numerics */
#define RPL_ADMINME 256
    CMSG("256", ":Administrative info about %s");
#define RPL_ADMINLOC1 257
    CMSG("257", ":%s");
#define RPL_ADMINLOC2 258
    CMSG("258", ":%s");
#define RPL_ADMINEMAIL 259
    CMSG("259", ":%s");

    return 1;
}
MODULE_UNLOADER(admin) {

    DMSG(RPL_ADMINME);
    DMSG(RPL_ADMINLOC1);
    DMSG(RPL_ADMINLOC2);
    DMSG(RPL_ADMINEMAIL);
}

/* argv[1] = server to request from */
CLIENT_COMMAND(admin, 0, 1, COMMAND_FL_REGISTERED) {

    if (pass_command(cli, NULL, "ADMIN", "%s", argc, argv, 1) !=
            COMMAND_PASS_LOCAL)
        return 0; /* sent along.. */

    sendto_one(cli, RPL_FMT(cli, RPL_ADMINME), ircd.me->name);
    if (*ircd.admininfo.line1 != '\0')
        sendto_one(cli, RPL_FMT(cli, RPL_ADMINLOC1), ircd.admininfo.line1);
    if (*ircd.admininfo.line2 != '\0')
        sendto_one(cli, RPL_FMT(cli, RPL_ADMINLOC2), ircd.admininfo.line2);
    if (*ircd.admininfo.line3 != '\0')
        sendto_one(cli, RPL_FMT(cli, RPL_ADMINEMAIL), ircd.admininfo.line3);

    return COMMAND_WEIGHT_LOW;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
