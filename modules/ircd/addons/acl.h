/*
 * acl.h: functions to allow adding/removing/checking ACLs
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: acl.h 830 2009-01-25 23:08:01Z wd $
 */

#ifndef IRCD_ADDONS_ACL_H
#define IRCD_ADDONS_ACL_H

typedef struct acl {
#define ACL_STAGE_CONNECT 1
#define ACL_STAGE_PREREG 2
#define ACL_STAGE_REGISTER 3
    int     stage;                  /* one of 1, 2, or 3 */
#define ACL_ACCESS_ANY -1
#define ACL_DENY 0
#define ACL_ALLOW 1
    int     access;

    unsigned short rule;            /* rule number of the acl */
    uint32_t hash;                  /* hash of the acl */

#define ACL_USERLEN (USERLEN * 2)
#define ACL_HOSTLEN HOSTLEN

    class_t *cls;                   /* the class clients in the ACL are
                                       placed in (optional) */
    char    user[ACL_USERLEN + 1];      /* username to match against (optional) */
    char    host[ACL_HOSTLEN + 1];      /* hostname/mask to match against */
    char    *reason;                /* ban reason */
    char    *info;                  /* "info" line to match against (stage 3
                                       only) */
    char    *pass;                  /* password */
    char    *redirect;              /* redirect server (or NULL if none) */
    int     redirect_port;          /* redirect port */

    time_t  added;                  /* when it was added */
    time_t  expire;                 /* when this will expire */
    timer_ref_t timer;              /* the timer handling the expiration */
#define ACL_FL_SKIP_DNS            0x01
#define ACL_FL_SKIP_IDENT   0x02
    int     flags;                  /* flags for the ACL.  Internal only. */

    char    *type;                  /* the type of the acl (just points to
                                       the actual string */
#define ACL_CONF_TEMP (conf_list_t *)0x1
    conf_list_t *conf;              /* the conf this acl came from, if any,
                                       NULL otherwise. */

    LIST_ENTRY(acl) intlp;          /* list pointers.  intlp is for internal
                                       (stage-segregated) lists */
    LIST_ENTRY(acl) lp;             /* lp is for the 'big list'. */
} acl_t;

LIST_HEAD(acl_list, acl);

extern struct acl_module_data {
    struct acl_list *stage1_list;
    struct acl_list *stage2_list;
    struct acl_list *stage3_list;
    struct acl_list *list;

    unsigned short default_rule;
} acl;

#define ACL_ANY_RULE -1
#define ACL_DEFAULT_RULE -2
acl_t *create_acl(int, int, char *, const char *, int);
acl_t *find_acl(int, int, char *, const char *, int, char *, char *);
void destroy_acl(acl_t *);
void acl_add_timer(acl_t *, time_t);
void acl_force_check(int, const acl_t *, const char *, bool);
HOOK_FUNCTION(acl_stage1_hook);
HOOK_FUNCTION(acl_stage2_hook);
HOOK_FUNCTION(acl_stage3_hook);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
