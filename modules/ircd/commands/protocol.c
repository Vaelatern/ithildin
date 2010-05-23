/*
 * protocol.c: the PROTOCOL command
 * 
 * Copyright 2008 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: ping.c 811 2007-09-04 03:29:58Z wd $");

MODULE_REGISTER("$Rev: 811 $");
/*
@DEPENDENCIES@: ircd
*/

/* argv[1]: protocol name
 * This command instigates a protocol change.  This can occur at any time
 * during the unregistered phase of a client connection. */
CLIENT_COMMAND(protocol, 1, 1, COMMAND_FL_UNREGISTERED) {
    char emsg[512];
    connection_t *cp = cli->conn;

    if (cp == NULL) {
        log_error("protocol command called by client with no connection");
        return COMMAND_WEIGHT_NONE;
    }

    protocol_t *proto;

    /* A missing protocol is considered to be a fatal error */
    if ((proto = find_protocol(argv[1])) == NULL) {
        snprintf(emsg, sizeof(emsg), "protocol %s not supported", argv[1]);
        destroy_connection(cp, emsg);
        return IRCD_CONNECTION_CLOSED;
    }

    set_connection_protocol(cp, proto);

    return IRCD_PROTOCOL_CHANGED;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
