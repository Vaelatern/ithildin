/*
 * umode_helper.h: macro for checking helper status
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: umode_helper.h 577 2005-08-21 06:33:51Z wd $
 */

#ifndef IRCD_ADDONS_UMODE_HELPER_H
#define IRCD_ADDONS_UMODE_HELPER_H

extern unsigned char usermode_helper;

#define ISHELPER(cli) (usermode_isset(cli, usermode_helper))

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
