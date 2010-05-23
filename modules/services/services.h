/*
 * services.h: Container for the overall services data structure
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: services.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef SERVICES_SERVICES_H
#define SERVICES_SERVICES_H

#include "../ircd/ircd.h"

typedef struct service service_t;
typedef struct regnick regnick_t;

#include "conf.h"
#include "db.h"
#include "mail.h"
#include "nick.h"
#include "oper.h"
#include "send.h"

enum service_type {
    NICK_SERVICE = 0,
    CHANNEL_SERVICE = 1,
    MEMO_SERVICE = 2,
    OPERATOR_SERVICE = 3
};

struct service {
    client_t *client;
    conf_list_t *conf;
    enum service_type type;
};

extern struct services_struct {
    conf_list_t **confhead;        /* head of our configuration tree */

    struct {
        char    file[PATH_MAX];                    /* this holds the location of the
                                               database file. */
        time_t        last;                            /* time the database was last
                                               synchronized. */

        struct {
            LIST_HEAD(, mail_contact) mail;
            struct regnick_list nicks;
        } list;
        struct {
            hashtable_t *mail;
            hashtable_t *nick;
        } hash;
    } db;
    /* herein are the expiration times for nicknames/channels/etc */
    struct {
        time_t        nick;
        time_t        chan;
        time_t        memo;
    } expires;
    struct {
        char        host[FQDN_MAXLEN + 1];            /* the host of our mailserver */
        char        port[NI_MAXSERV];            /* its port */
        char        from[HOSTLEN * 2];            /* Address to send from */
        time_t        interval;                    /* interval at which to send mail
                                               for nicknames */

        struct {
            char    *activate;                    /* the file holding the activation
                                               template. */
        } templates;
    } mail;
    struct {
        struct mdext_item *client;            /* for clients */
    } mdext;
    struct {
        time_t        mail_sends;                    /* same-address mail time */
        int        mail_nicks;                    /* nicknames per address */
        int        linked_nicks;                    /* child nicks per parent */
        int        nick_acclist;                    /* access list entries */
    } limits;

    service_t nick;
    service_t oper;
} services;

/* this macro is used in various places for services command functions */
#define SERVICES_COMMAND(_func)                                                \
    void _func(client_t *cli, int argc, char **argv)

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
