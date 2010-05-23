/*
 * user.c: the USER command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: user.c 818 2008-09-21 22:00:54Z wd $");

MODULE_REGISTER("$Rev: 818 $");
/*
@DEPENDENCIES@: ircd
*/

CLIENT_COMMAND(user, 4, 4, COMMAND_FL_UNREGISTERED) {
    connection_t *cp = cli->conn;
    char *s, *u;
    int len = USERLEN;

    /* if an ident check was performed (and failed) cp->user will be "~".  If a
     * check was performed and successful cp->user will be something else.  If
     * a check was not performed cp->user will be empty. */
    s = cli->user;
    if (*cp->user == '~') {
        *s++ = '~';
        u = argv[1];
        len--;
    } else if (*cp->user != '\0')
        u = cp->user;
    else
        u = argv[1];

    /* now check their user string and clean out any garbage that might be in
     * it.  Basically A-Za-z0-9_.- are okay, anything else is not.  Right now
     * we don't do mixed case checking or any of the other complicated stuff in
     * other servers.  Also, we are friendly and simply strip out crud instead
     * of dropping the user for having it. */
    while (len && *u) {
        if (isalnum(*u) || strchr("_.-", *u)) {
            *s++ = *u;
            len--;
        }
        u++;
    }
    *u = '\0';

    u = (*cli->user != '~' ? cli->user : cli->user + 1);
    if (*u == '\0')
        strcpy(u, "null");

    /* fill in other stuff if it hasn't already been set */
    if (*cli->host == '\0')
        strncpy(cli->host, cp->host, HOSTLEN);
    strncpy(cli->info, argv[4], GCOSLEN);
    cli->server = ircd.me;

    /* if they sent a nick before register them */
    if (*cli->nick != '\0')
        return register_client(cli);

    return COMMAND_WEIGHT_NONE;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
