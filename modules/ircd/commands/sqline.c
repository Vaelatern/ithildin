/*
 * sqline.c: the SQLINE/UNSQLINE command.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/quarantine.h"

IDSTRING(rcsid, "$Id: sqline.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/quarantine
*/

static const char *sqline_quarantine_type = "sqline";
HOOK_FUNCTION(sqline_se_hook);

MODULE_LOADER(sqline) {

    add_command_alias("sqline", "unsqline");

    add_hook(ircd.events.server_establish, sqline_se_hook);

    return 1;
}
MODULE_UNLOADER(sqline) {

    remove_hook(ircd.events.server_establish, sqline_se_hook);
}

/* we actually serve for both SQLINE and UNSQLINE commands both. */
SERVER_COMMAND(sqline, 1, 0, 0) {
    quarantine_t *qp;

    if (!strcasecmp(argv[0], "SQLINE")) {
        /* arguments: mask [reason] */
        qp = add_quarantine(argv[1]);

        if (argc > 2)
            qp->reason = strdup(argv[2]);
        else
            qp->reason = strdup("Reserved");

        qp->type = strdup(sqline_quarantine_type);
        sendto_serv_butone(sptr, NULL, srv, NULL, "SQLINE", "%s :%s", qp->mask,
                qp->reason);
    } else if (!strcasecmp(argv[0], "UNSQLINE")) {
        /* arguments: mask */
        quarantine_t *qp2;

        /* much like UNSGLINE, we accept masks here, so we have to walk the
         * whole quarantine list to see about removing them. */
        if (check_channame(argv[1]))
            qp = LIST_FIRST(quarantine.channels);
        else
            qp = LIST_FIRST(quarantine.nicks);
        while (qp != NULL) {
            qp2 = LIST_NEXT(qp, lp);
            if (qp->type != NULL && !strcmp(qp->type, sqline_quarantine_type)
                    && match(argv[1], qp->mask))
                remove_quarantine(qp);
            qp = qp2;
        }
    }

    return 0;
}

HOOK_FUNCTION(sqline_se_hook) {
    server_t *srv = (server_t *)data;
    quarantine_t *qp;

    LIST_FOREACH(qp, quarantine.nicks, lp) {
        if (qp->type != NULL && !strcmp(qp->type, sqline_quarantine_type))
            sendto_serv(srv, "SQLINE", "%s :%s", qp->mask, qp->reason);
    }
    LIST_FOREACH(qp, quarantine.channels, lp) {
        if (qp->type != NULL && !strcmp(qp->type, sqline_quarantine_type))
            sendto_serv(srv, "SQLINE", "%s :%s", qp->mask, qp->reason);
    }

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
