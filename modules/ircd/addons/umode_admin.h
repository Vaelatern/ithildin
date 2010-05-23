/*
 * umode_admin.h: macro for checking server admin status on nicks
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: umode_admin.h 577 2005-08-21 06:33:51Z wd $
 */

#ifndef IRCD_ADDONS_UMODE_ADMIN_H
#define IRCD_ADDONS_UMODE_ADMIN_H

extern unsigned char usermode_admin;

#define ISADMIN(cli) (usermode_isset(cli, usermode_admin))

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
