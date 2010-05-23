/*
 * mail.h: email handling definitions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: mail.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef SERVICES_MAIL_H
#define SERVICES_MAIL_H

struct mail_contact {
    char    address[HOSTLEN * 2 + 1];        /* the address itself */
    int            nicks;                        /* number of nicks registered for this
                                           address. */
    time_t  last;                        /* last time email was sent to this
                                           address. */
    
    LIST_ENTRY(mail_contact) lp;
};

struct mail_contact *create_mail_contact(char *);
void destroy_mail_contact(struct mail_contact *);

void mail_setup(void);
void mail_send(void);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
