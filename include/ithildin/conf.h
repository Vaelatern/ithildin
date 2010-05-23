/*
 * conf.h: configuration parser structures/prototypes
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: conf.h 578 2005-08-21 06:37:53Z wd $
 */

#ifndef CONF_H
#define CONF_H

#include "queue.h"

/*
 * the makings of a configuration tree are presented here, there is
 * basically a configuration structure, which contains either a pointer to
 * another configuration structure (a substructure), or the data in string
 * format.
 */

typedef struct conf_list conf_list_t;
typedef struct conf_entry conf_entry_t;

/* first create the conf_list type using queue functions, this will be the
 * head of a tree, or a sub-tree */
LIST_HEAD(conf_list, conf_entry);

/* this is the structure used for any single node, it contains either a
 * sub-tree, or an actual single entry of data which is terminating. */
struct conf_entry {
    char    *name;        /* the name of the entry */
    char    *string;    /* string (if this is a single data entry) */
    conf_list_t *list;  /* list of stuff (if this is a list entry) string
                           may also be filled out if this list has a
                           name */
    conf_list_t *parent;/* parent of this entry, this should never be
                           null! */

#define CONF_TYPE_LIST 0x1
#define CONF_TYPE_DATA 0x2
    int        type;

    LIST_ENTRY(conf_entry) lp;

};        

/* general conf handling calls */
extern conf_list_t *read_conf(char *);
void destroy_conf_branch(conf_list_t *);
void conf_display_tree(int, conf_list_t *);

/* these are conf search functions */
conf_entry_t *conf_find(const char *, const char *, int, conf_list_t *, int);
conf_entry_t *conf_find_next(const char *, const char *, int, conf_entry_t *,
        conf_list_t *, int);
conf_list_t *conf_find_list(const char *, conf_list_t *, int);
conf_list_t *conf_find_list_next(const char *, conf_list_t *, conf_list_t *,
        int);
char *conf_find_entry(const char *, conf_list_t *, int);
char *conf_find_entry_next(const char *, char *, conf_list_t *, int);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
