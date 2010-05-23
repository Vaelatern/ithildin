/*
 * db.h: database function declarations
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: db.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef SERVICES_DB_H
#define SERVICES_DB_H

/* These functions are used to find various structures in the database. */
#define db_find_mail(_addr)                                                \
    (struct mail_contact *)hash_find(services.db.hash.mail, _addr)
#define db_find_nick(_nick)                                                \
    (regnick_t *)hash_find(services.db.hash.nick, _nick)

int db_start(void);
void db_cleanup(void);
void db_sync(void);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
