/*
 * oper.h: operator service stuff
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: oper.h 577 2005-08-21 06:33:51Z wd $
 */

#ifndef SERVICES_OPER_H
#define SERVICES_OPER_H

void oper_setup(void);
void oper_handle_msg(client_t *, int, char **);
#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
