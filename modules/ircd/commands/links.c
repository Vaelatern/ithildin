/*
 * links.c: the LINKS command.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: links.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

MODULE_LOADER(links) {

#define RPL_LINKS 364
    CMSG("364", "%s %s :%d %s");
#define RPL_ENDOFLINKS 365
    CMSG("365", "%s :End of /LINKS list.");

    return 1;
}
MODULE_UNLOADER(links) {

    DMSG(RPL_LINKS);
    DMSG(RPL_ENDOFLINKS);
}

/* the links command.  displays servers which are currently..um...linksed. ;)
 * argv[1] ?= server name mask
 *
 * Historically, this command allowed the client to request a links from
 * other servers.  I've removed that behavior, as it only seems useful to
 * create unnecessary network load. */
CLIENT_COMMAND(links, 0, 1, COMMAND_FL_REGISTERED) {
    char *mask = (argc > 1 ? argv[1] : "*");
    server_t *sp;

    LIST_FOREACH(sp, ircd.lists.servers, lp) {
        if (match(mask, sp->name) && CAN_SEE_SERVER(cli, sp))
            sendto_one(cli, RPL_FMT(cli, RPL_LINKS), sp->name,
                    CAN_SEE_SERVER(cli, sp->parent) ? sp->parent->name :
                    "<hidden>", sp->hops, sp->info);
    }
    if (match(mask, ircd.me->name))
        sendto_one(cli, RPL_FMT(cli, RPL_LINKS), ircd.me->name,
                    ircd.me->name, 0, ircd.me->info);
    sendto_one(cli, RPL_FMT(cli, RPL_ENDOFLINKS), mask);

    return COMMAND_WEIGHT_HIGH;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
