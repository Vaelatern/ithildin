/*
 * servicesid.h: client servicesid tracker (actually just data-holder)
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: servicesid.h 577 2005-08-21 06:33:51Z wd $
 */

#ifndef IRCD_ADDONS_SERVICESID_H
#define IRCD_ADDONS_SERVICESID_H

extern struct mdext_item *servicesid_mdext;
#define SVSID(cli) *(uint32_t *)(mdext(cli, servicesid_mdext))

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
