/*
 * send.h: functions for sending replies and stuff
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: send.h 577 2005-08-21 06:33:51Z wd $
 */

#ifndef SERVICES_SEND_H
#define SERVICES_SEND_H

void send_reply(client_t *, service_t *, char *, ...) __PRINTF(3);
void send_opnotice(service_t *, char *, ...) __PRINTF(2);
void send_help(client_t *, service_t *, int);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
