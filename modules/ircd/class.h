/*
 * class.h: connection class structure definitions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: class.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef IRCD_CLASS_H
#define IRCD_CLASS_H

struct class {
    char    name[32];                /* name of the class */
    int            freq;                /* ping-timeout frequency in seconds */
    int            max;                /* maximum clients in this class */
    int            flood;                /* maximum flood weight in this class */
    int            clients;                /* current number of clients in this class
                                   (may be > max) */
    int            sendq;                /* maximum number of sendqueue items */
    char    *default_mode;        /* default modes for users in this class */
    struct message_set *mset;        /* the suggested message set, by default this
                                   is the default set */
    struct privilege_set *pset; /* the suggested privilege set, by default
                                   this is the default set */
    char    *mdext;                /* extended data */
    conf_list_t *conf;                /* the conf this class came from */
    int            dead;                /* this is set if the class should be deleted
                                   at 0 clients */

    LIST_ENTRY(class) lp;
};

class_t *create_class(char *);
void destroy_class(class_t *);
void add_to_class(class_t *, connection_t *);
void del_from_class(connection_t *);
class_t *find_class(char *);

char **class_mdext_iter(char **);
#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
