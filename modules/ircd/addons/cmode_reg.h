/*
 * cmode_reg.h: macro for checking registration status on channels
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: cmode_reg.h 613 2005-11-22 13:43:19Z wd $
 */

#ifndef IRCD_ADDONS_CMODE_REG_H
#define IRCD_ADDONS_CMODE_REG_H

extern unsigned char reg_cmode;

#define ISREGCHAN(chan) (chanmode_isset(chan, reg_cmode))

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
