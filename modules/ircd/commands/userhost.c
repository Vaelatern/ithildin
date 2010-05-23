/*
 * userhost.c: the USERHOST command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "commands/away.h"

IDSTRING(rcsid, "$Id: userhost.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/core
*/

MODULE_LOADER(userhost) {

    /* now create numerics */
#define RPL_USERHOST 302
    CMSG("302", ":%s");

    return 1;
}
MODULE_UNLOADER(userhost) {

    DMSG(RPL_USERHOST);
}

/* this is a really obnoxious command. either argv1 is a space separated list
 * of clients to USERHOST, or argv[1..argc - 1] are individual clients to
 * userhost.  I've tried to remain behavior-compatible with other userhost
 * implementations.  Blah */
CLIENT_COMMAND(userhost, 1, 0, COMMAND_FL_REGISTERED) {
    client_t *target;
    char *cur, *next;
    char buf[512];
    int len = 0;
    int count = 0;
    int oarg = 2;

    /* if they specified their arguments without a :, then we'll probably have
     * a bunch.of arguments.  keep calling the command with a 'moved' argv to
     * simulate replies. */
    *buf = '\0';
    cur = argv[1];
    next = strchr(cur, ' ');

    while (cur != NULL) {
        if (next != NULL)
            *next++ = '\0';

        /* userhost doesn't error if the target isn't found, apparently */
        if ((target = find_client(cur)) != NULL)
            len += snprintf(buf + len,  512 - len, "%s%s=%c%s@%s ",
                    target->nick, OPER(target) ? "*" : "", 
                    AWAYMSG(target) ? '-' : '+', target->user, target->host);

        if (next == NULL && argc > oarg)
            next = argv[oarg++];
        if (++count == 5) /* stop after five people */
            break;
        else if (next != NULL && *next) {
            cur = next;
            next = strchr(cur, ' ');
            continue;
        }
        break;
    }
    buf[len - 1] = '\0'; /* null terminate. */

    if (*buf) /* only reply if we found someone */
        sendto_one(cli, RPL_FMT(cli, RPL_USERHOST), buf);

    return COMMAND_WEIGHT_MEDIUM + (COMMAND_WEIGHT_LOW * count);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
