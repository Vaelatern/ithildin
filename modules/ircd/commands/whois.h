/*
 * whois.h: just a header to hold the 'whois' event declaration
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: whois.h 492 2004-01-10 16:15:06Z wd $
 */

#ifndef IRCD_COMMANDS_WHOIS_H
#define IRCD_COMMANDS_WHOIS_H

/* this is it. ;) */
extern event_t *whois_event;

/* almost.. ;) */
#define RPL_WHOISSERVER 312
#define RPL_WHOISACTUALLY 338

#endif
