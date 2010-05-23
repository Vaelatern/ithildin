/*
 * help.h: help data management functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: help.h 577 2005-08-21 06:33:51Z wd $
 */

#ifndef SERVICES_HELP_H
#define SERVICES_HELP_H

int create_help_topic(service_t *, char *, char *);
void parse_help(client_t *, service_t *, int, char **);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
