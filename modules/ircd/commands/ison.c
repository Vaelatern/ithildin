/*
 * ison.c: the ISON command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: ison.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

MODULE_LOADER(ison) {

#define RPL_ISON 303
    CMSG("303", ":%s");

    return 1;
}
MODULE_UNLOADER(ison) {

    DMSG(RPL_ISON);
}

/* argv[1] == space separated list of clients to ISON.  We have to fold
 * because old servers didn't allow the :list form (rfc1459) and a lot of
 * clients worked around that.  ugh. */
CLIENT_COMMAND(ison, 1, 1, COMMAND_FL_REGISTERED | COMMAND_FL_FOLDMAX) {
    client_t *target;
    char *cur, *next;
    char buf[512];
    int len = 0;

    *buf = '\0';
    cur = argv[1];
    next = strchr(cur, ' ');

    while (cur != NULL) {
        if (next != NULL)
            *next++ = '\0';

        /* no error if client not found */
        if ((target = find_client(cur)) != NULL)
            len += snprintf(&buf[len], 512 - len, "%s ", target->nick);

        if (len >= 512)
            break; /* stop when we run out of room */

        if (next != NULL && *next) {
            cur = next;
            next = strchr(cur, ' ');
            continue;
        }
        break;
    }
    buf[len - 1] = '\0'; /* null terminate. */

    sendto_one(cli, RPL_FMT(cli, RPL_ISON), buf);

    return COMMAND_WEIGHT_MEDIUM;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
