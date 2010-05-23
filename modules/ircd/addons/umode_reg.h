/*
 * umode_reg.h: macro for checking registration status on nicks
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: umode_reg.h 619 2005-11-22 18:40:33Z wd $
 */

#ifndef IRCD_ADDONS_UMODE_REG_H
#define IRCD_ADDONS_UMODE_REG_H

extern unsigned char reg_umode;

#define ISREGNICK(cli) (usermode_isset(cli, reg_umode))

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
