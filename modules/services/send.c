/*
 * send.c: various server output routines
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains some simple-ish routines for sending output in various
 * manners.
 */

#include <ithildin/stand.h>

#include "services.h"

IDSTRING(rcsid, "$Id: send.c 579 2005-08-21 06:38:18Z wd $");

void send_reply(client_t *cli, service_t *sp, char *msg, ...) {
    char fmsg[512];
    va_list vl;

    va_start(vl, msg);
    vsnprintf(fmsg, 512, msg, vl);
    sendto_one_from(cli, sp->client, NULL, "NOTICE", ":%s", fmsg);
    va_end(vl);
}

void send_opnotice(service_t *sp, char *msg, ...) {
    char fmsg[512];
    va_list vl;

    va_start(vl, msg);
    vsnprintf(fmsg, 512, msg, vl);
    sendto_serv_butone(NULL, (sp == NULL ? NULL : sp->client),
            (sp == NULL ? ircd.me : NULL), NULL,
            (sp == NULL ? "GNOTICE" : "GLOBOPS"), ":%s", fmsg);
    va_end(vl);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
