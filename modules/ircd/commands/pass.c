/*
 * pass.c: the PASS command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: pass.c 778 2006-10-02 00:41:20Z wd $");

MODULE_REGISTER("$Rev: 778 $");
/*
@DEPENDENCIES@: ircd
*/

static void copy_in_pass(connection_t *, char *);

CLIENT_COMMAND(pass, 1, 1, COMMAND_FL_UNREGISTERED) {

    /* just copy the pass in */
    copy_in_pass(cli->conn, argv[1]);
    return COMMAND_WEIGHT_NONE;
}

SERVER_COMMAND(pass, 1, 1, COMMAND_FL_UNREGISTERED) {

    /* just copy the pass in */
    copy_in_pass(srv->conn, argv[1]);
    return 0;
}

static void copy_in_pass(connection_t *conn, char *pass) {

    /* Hack to support CGI IRC, which sends the 'real hostname' as the client
     * password.  What?  Let's use HOSTLEN I guess! */
    if (conn->pass != NULL)
        free(conn->pass); /* dump whatever was there before.. */
    conn->pass = strdup(pass);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
