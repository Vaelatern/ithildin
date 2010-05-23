/*
 * conf.h: configuration-specific constructs
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: conf.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef IRCD_CONF_H
#define IRCD_CONF_H

typedef struct hostlist hostlist_t;
struct hostlist {
    char *name;                        /* name of the host list */
    int entries;                /* number of entries in the list */
    char **list;                /* the 'list' of entries. */

    LIST_ENTRY(hostlist) lp;
};

hostlist_t *create_host_list(char *);
hostlist_t *find_host_list(char *);
void destroy_host_list(hostlist_t *);
void add_to_host_list(hostlist_t *, char *);
void del_from_host_list(hostlist_t *, char *);

/* non-hostlist stuff */
int ircd_parse_conf(conf_list_t *); 

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
