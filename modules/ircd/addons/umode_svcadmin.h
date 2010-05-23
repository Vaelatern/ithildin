/*
 * umode_svcadmin.h: macro for checking services admin status on nicks
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: umode_svcadmin.h 577 2005-08-21 06:33:51Z wd $
 */

#ifndef IRCD_ADDONS_UMODE_SVCADMIN_H
#define IRCD_ADDONS_UMODE_SVCADMIN_H

extern unsigned char usermode_svcadmin;

#define ISSVCADMIN(cli) (usermode_isset(cli, usermode_svcadmin))

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
