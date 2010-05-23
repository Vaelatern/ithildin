/*
 * nick.h: registered nickname definitions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: nick.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef SERVICES_NICK_H
#define SERVICES_NICK_H

/* This is the structural container for registered nicknames.  Nicknames are
 * tied in to user structures using mdexts. */
LIST_HEAD(regnick_list, regnick);
struct regnick {
    char    name[NICKLEN + 1];                /* the nickname.  never changes. */
    char    user[USERLEN + 1];                /* the last seen username.. */
    char    host[HOSTLEN + 1];                /* ... and hostname... */
    char    info[GCOSLEN + 1];                /* ... and gecos info. */
    char    pass[PASSWDLEN + 1];        /* their nick pass */
    struct mail_contact *email;                /* email data */

    time_t  last;                        /* last seen timestamp */
    time_t  regtime;                        /* time of registration */

#define NICK_FL_SHOWEMAIL   0x0001
#define NICK_FL_PROTECT            0x0002
    int64_t flags;                        /* external flags */

#define NICK_IFL_ADMIN            0x0001
#define regnick_admin(x) ((x)->intflags & NICK_IFL_ADMIN)
#define NICK_IFL_BANISHED   0x0002
#define NICK_IFL_HELD            0x0004
#define NICK_IFL_SYNCED            0x0008
#define NICK_IFL_MAILED            0x0010
#define NICK_IFL_ACTIVATED  0x0020
#define regnick_activated(x) ((x)->intflags & NICK_IFL_ACTIVATED)
#define NICK_IFL_MASKACCESS 0x0040
#define NICK_IFL_IDACCESS   0x0080
#define NICK_IFL_ACCESS            (NICK_IFL_MASKACCESS | NICK_IFL_IDACCESS)
#define regnick_access(x) ((x)->intflags & \
        (NICK_IFL_MASKACCESS | NICK_IFL_IDACCESS))
#define regnick_idaccess(x) ((x)->intflags & NICK_IFL_IDACCESS)

#define NICK_IFL_NOSAVE            (NICK_IFL_ADMIN | NICK_IFL_SYNCED |                \
        NICK_IFL_MASKACCESS | NICK_IFL_IDACCESS)

    int64_t intflags;                        /* special internal flags */

    SLIST_HEAD(, regnick_access) alist; /* the access list */

    struct regnick *parent;                /* the parent nick.  NULL if this is
                                           not linked on to a nick. */
    struct regnick_list links;                /* linked in nicknames */
    LIST_ENTRY(regnick) linklp;

    LIST_ENTRY(regnick) lp;
};

regnick_t *regnick_create(char *);
void regnick_destroy(regnick_t *);

/* This is a simple structure which holds access list entries for registered
 * nicknames.  Nothing too complicated here. */
struct regnick_access {
    SLIST_ENTRY(regnick_access) lp;
    size_t size;                        /* the size of the entire structure
                                           (NOT the length of the string) */
    char user[USERLEN + 1];                /* the username.  static because it's
                                           fairly small. */
    char host[2];                        /* the hostname.  actually variably
                                           sized on allocation. */
};

struct regnick_access *regnick_access_add(regnick_t *, char *);
struct regnick_access *regnick_access_find(regnick_t *, char *);
void regnick_access_del(regnick_t *, struct regnick_access *);
void regnick_access_check(client_t *, regnick_t *);

/* This is the structure which is added to the client data field of the IRC
 * server.  It is used to track lookups on behalf of the user, the nickname the
 * user is currently identified for, and anything else that might be useful */
struct services_client_data {
    struct regnick *nick;
};

void nick_setup(void);
void nick_handle_msg(client_t *, int, char **);
HOOK_FUNCTION(nick_register_hook);

int has_access_to_nick(client_t *, regnick_t *, int64_t);
#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
