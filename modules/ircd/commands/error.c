/*
 * error.c: the ERROR command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: error.c 718 2006-04-25 11:44:28Z wd $");

MODULE_REGISTER("$Rev: 718 $");
/*
@DEPENDENCIES@: ircd
*/

/* the error command basically is just used to say "something bad happened."
 * It is usually issued prior to a connection being dropped, but may come from
 * other places too. */
SERVER_COMMAND(error, 1, 1, COMMAND_FL_UNREGISTERED) {

    sendto_flag(ircd.sflag.ops, "ERROR :from %s -- %s", srv->name, argv[1]);
    /* ERROR means destroy the connection. :) */
    destroy_server(srv, argv[1]);
    return IRCD_CONNECTION_CLOSED;
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
