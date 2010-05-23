/*
 * conf.h: configuration reader functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: conf.h 577 2005-08-21 06:33:51Z wd $
 */

#ifndef SERVICES_CONF_H
#define SERVICES_CONF_H

int services_parse_conf(conf_list_t *);
void register_service(service_t *);
#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
