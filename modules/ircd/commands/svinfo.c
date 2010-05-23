/*
 * svinfo.c: the SVINFO command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: svinfo.c 718 2006-04-25 11:44:28Z wd $");

MODULE_REGISTER("$Rev: 718 $");
/*
@DEPENDENCIES@: ircd
*/

#define MINIMUM_TS_VERSION 3
/* I'm not making these easily configurable because they *should not be* easily
 * configurable.  If the time deltas are off then the machines in question
 * ought to be running a network time synchronization system.  Having incorrect
 * time data is very hazardous to an IRC network.  Do not bitch to me because
 * your servers aren't timesynched. */
#define MAX_TS_DELTA 120
#define WARN_TS_DELTA 15

/* This command provides information on servers.  Specifically it provides the
 * TS version in use, the minimum supported, and the timestamp of the server as
 * of the time the command is sent.  We are particularly interested in the
 * timestamp, and examine it against two values (a warning level and an
 * unacceptable error level) */
SERVER_COMMAND(svinfo, 4, 4, COMMAND_FL_UNREGISTERED) {
    time_t delta;

    if (!MYSERVER(sptr) || sptr->conn == NULL) /* hmm.. */
        return 0;

    if (str_conv_int(argv[1], 0) < MINIMUM_TS_VERSION) {
        sendto_flag(SFLAG("GNOTICE"),
                "Link %s dropped, wrong TS protocol version (%s,%s)",
                sptr->conn->host, argv[1], argv[2]);
        destroy_server(sptr, "Incompatible TS version");
        return IRCD_CONNECTION_CLOSED;
    }

    if ((delta = abs(me.now - str_conv_int(argv[4], 0))) >= WARN_TS_DELTA)
        /* Complain loudly, but don't drop the link. */
        sendto_flag(SFLAG("GNOTICE"), "Link %s notable TS delta (my TS=%d, "
                "their TS=%s, delta=%d)", sptr->conn->host, me.now, argv[4],
                delta);
    else if (delta >= MAX_TS_DELTA) {
        sendto_flag(SFLAG("GNOTICE"), "Link %s dropped, excessive TS delta "
               "(my TS=%d, their TS=%s, delta=%d)", sptr->conn->host, me.now,
               argv[4], delta);
        sendto_serv_butone(sptr, NULL, ircd.me, NULL, "GNOTICE",
                ":Link %s dropped, excessive TS delta (delta=%d)",
                sptr->conn->host, delta);
        destroy_server(sptr, "Excessive TS delta");
        return IRCD_CONNECTION_CLOSED;
    }

    return 0;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
