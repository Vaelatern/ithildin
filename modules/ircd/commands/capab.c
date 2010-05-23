/*
 * capab.c: the CAPAB command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: capab.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

CLIENT_COMMAND(capab, 1, 0, COMMAND_FL_UNREGISTERED) {

    return COMMAND_WEIGHT_NONE; /* nothing to say here.. */
}

/* This will read in a list of capabilities from the server and modify the
 * server's pflags area as necessary. */
SERVER_COMMAND(capab, 1, 0, COMMAND_FL_UNREGISTERED) {
    int i = 1;

    if (!MYSERVER(sptr))
        return 0;

    while (i < argc) {
        if (!strcasecmp(argv[i], "ATTR"))
            sptr->pflags |= PROTOCOL_SFL_ATTR;
        else if (!strcasecmp(argv[i], "NOQUIT"))
            sptr->pflags |= PROTOCOL_SFL_NOQUIT;
        else if (!strcasecmp(argv[i], "SJOIN") ||
                !strcasecmp(argv[i], "SSJOIN"))
            sptr->pflags |= PROTOCOL_SFL_SJOIN;
        else if (!strcasecmp(argv[i], "TSMODE"))
            sptr->pflags |= PROTOCOL_SFL_TSMODE;
        i++;
    }

    return 0;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
