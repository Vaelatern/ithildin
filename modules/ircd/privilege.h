/*
 * privilege.h: support structures/prototypes for privilege.c
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: privilege.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef IRCD_PRIVILEGE_H
#define IRCD_PRIVILEGE_H

/* The privilege system works a lot like the message-set system.  The user
 * creates configured 'sets' of privileges, and these are referenced via an
 * array created on a per-set basis.  Privileges, however, behave slightly
 * differently, as there are several sorts available.  Currently, privilege
 * data may be stored as a boolean (a char), a number (64 bit integer), or a
 * number/word tuple (actually just a number, but with special handling for
 * conf parsing purposes) */

/* Each privilege set has a name, and an array of data values stored as void
 * pointers. */
struct privilege_set {
    char    *name;
    void    **vals;

    LIST_ENTRY(privilege_set) lp;
};

/* these functions create, find, and destroy a privilege set, respectively.
 * the create function uses the given conf to set either configured or
 * default values for all privileges. */
privilege_set_t *create_privilege_set(char *, conf_list_t *);
privilege_set_t *find_privilege_set(char *);
void destroy_privilege_set(privilege_set_t *);

/* this is a privilege tuple, used below.  It simply holds name/value pairs for
 * mapping purposes. */
struct privilege_tuple {
    char    *name;            /* name of the tuple */
    int64_t val;            /* value of the tuple */
};

struct privilege {
    char    *name;            /* the name of the privilege, typically associated
                               with a command (but not always) */
    int     num;            /* the privilege's number in a privilege set
                               array */
    int            flags;            /* flags for the privilege */
#define PRIVILEGE_FL_BOOL   0x0001
#define PRIVILEGE_FL_INT    0x0002
#define PRIVILEGE_FL_TUPLE  0x0004
#define PRIVILEGE_FL_STR    0x0008

    void    *default_val;   /* the default value of the privilege */
    struct privilege_tuple *tuples; /* array of privilege tuples */
};

int create_privilege(char *, int, void *, void *);
void destroy_privilege(int);
int find_privilege(char *);

/* below are macros for grabbing privileges from structures.  They assume the
 * given structure has a 'pset' pointer which points to a privilege set.  They
 * can be used to cast values properly, etc.  TPRIV and IPRIV are equivalnet.
 * If the given structure is NULL, they will use the default privilege set
 * defined in the default ircd class.
 */

/* this one returns the privilege as-is (as a void *) */
#define PRIV(ent, index)                                                      \
    (ent != NULL ? ent->pset->vals[index] :                                   \
     ircd.privileges.privs[index].default_val)

/* these four are casts */
#define IPRIV(ent, index) *(int64_t *)PRIV(ent, index)
#define BPRIV IPRIV
#define TPRIV IPRIV
#define SPRIV(ent, index) (char *)PRIV(ent, index)

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
