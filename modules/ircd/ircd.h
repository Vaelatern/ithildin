/*
 * ircd.h: File containing the structure definition for the ircd meta-data
 * holder
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: ircd.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef IRCD_IRCD_H
#define IRCD_IRCD_H

/* various size definitions */
#define SERVLEN 63
#define USERLEN 10
#define HOSTLEN SERVLEN
#define GCOSLEN 50
#define PASSWDLEN 33
#define TOPICLEN 384
#define NICKLEN 30 /* maximum nick length, may be reduced by the user */
#define CHANLEN 32 /* maximum channel name length, may be reduced by the
                      user */

/* some typedefs for stuff found elsewhere */
typedef struct channel channel_t;
typedef struct class class_t;
typedef struct client client_t;
typedef struct privilege_set privilege_set_t;
typedef struct privilege privilege_t;
typedef struct protocol protocol_t;
typedef struct message_set message_set_t;
typedef struct message message_t;
typedef struct server server_t;
typedef struct connection connection_t;

#include "channel.h"
#include "class.h"
#include "client.h"
#include "command.h"
#include "conf.h"
#include "connection.h"
#include "ircstring.h"
#include "privilege.h"
#include "protocol.h"
#include "send.h"
#include "server.h"
#include "support.h"

HOOK_FUNCTION(ircd_listen_hook);
HOOK_FUNCTION(ircd_timer_hook);

/* the reason this structure is the way it is, is to prevent/avoid
 * unnecessary namespace polution, and to organize things from a reading
 * point of view.  Individual 'sections' are commented on as necessary. */
extern struct ircd_struct {
    server_t *me; /* our server structure, not linked anywhere! */

    char    address[HOSTLEN + 1];   /* our server's address (for listen()) */
    char    network[GCOSLEN + 1];   /* the name of our network */
    char    network_full[TOPICLEN + 1];/* the full network name. */
    const char *realversion;            /* the version number in the module. */
    char    version[GCOSLEN + 1];   /* user defined server version */
    char    vercomment[TOPICLEN + 1];/* user defined version 'comment' */
    char    statsfile[PATH_MAX];    /* stats file */
    int            started;                    /* this is set to 1 once the daemon has
                                       actually started */
    char    ascstart[48];            /* time started, used in RPL_CREATED and
                                       others, made once to save time */
    conf_list_t **confhead;            /* the head of our configuration tree */

    char    **argv;                    /* argv/argc for commands */
    int            argc;

    /* XXX(?):  (see 'triple-x inre 'tmpmsg' in the protocol structure, too).
     * This is another one of those hacks involving sendto_* functions.  When
     * sending to, for instance, a group of channels, it is difficult to
     * determine who has received the message more than once.  This string is
     * actually allocated at one character per connection, each time a new
     * sendto_* function which needs it is called, it, 0s the string, and
     * then sets each character to 1 as it sends messages along.  It might
     * actually make more sense to read the sendto_ functions that use this.
     * anyways, again, is this a hack or the best way? */
    char    *sends;

    struct {
        struct {
            int            curclients;        /* network-wide current/max clients */
            int            maxclients;
            int            visclients; /* -i clients */
        } net;
        struct {
            int            curclients; /* server current/max clients */
            int            maxclients;
            int            unkclients; /* unknown clients */
            int            servers;        /* servers we have on us */
        } serv;
        int        channels;
        int        servers;
        int        opers;
    } stats;

    struct {
        char        line1[GCOSLEN + 1];
        char        line2[GCOSLEN + 1];
        char        line3[GCOSLEN + 3];
    } admininfo;

    struct {
        int        nicklen;
        int        chanlen;
    } limits;

    /* usermode data */
    struct {
        unsigned char avail[64];        /* the available usermodes in string
                                           form (available for use, not for
                                           registering new modes with ;) */
        uint64_t i;                        /* the mask for +i */
        uint64_t o;                        /* the mask for +o */
        uint64_t s;                        /* the mask for +s */

        struct        usermode modes[256];        /* mode structures.  a mode can be
                                           found by doing ircd.umodes.modes[c],
                                           where 'c' is the mode character
                                           you're after */
    } umodes;

    /* channel mode data */
    struct {
        unsigned char avail[64 + 5];        /* available channel modes */
        unsigned char prefix[64];        /* available prefix goodies */
        unsigned char pmodes[32];        /* modes which are prefixes, this is
                                           mostly for internal use */

        struct        chanmode modes[256];        /* mode structures.  a mode can be
                                           found by doing ircd.cmodes.modes[c],
                                           where 'c' is the mode character
                                           you're after. */
        struct        chanmode *pfxmap[256];        /* map to chanmodes from their
                                           prefixes.  useful for fast lookups
                                           and such. */
    } cmodes;

    struct {
        /* called once when the server is completely started, this allows
         * necessary functions to be called when everything is ready */
        event_t        *started;

        /* the three connection stage events, used by the acl system, and
         * maybe others. */
        event_t        *stage1_connect;
        event_t        *stage2_connect;
        event_t        *stage3_connect;

        /* other events */
        event_t        *client_connect;    /* called after the stage*_connect to note
                                       the connection of a (local) client */
        event_t *client_disconnect; /* called when a registered client (local)
                                       is disconnecting from the server */
        event_t        *register_client;   /* called from register_client() for all
                                       connecting clients. */
        event_t *unregister_client; /* called from destroy_client() for all
                                       clients being destroyed */
        event_t *client_nick;            /* hooked when a client changes its
                                       nickname. */
        event_t *client_oper;            /* hooked when a client goes +o */
        event_t *client_deoper;            /* hooked when a client goes -o */
        event_t *channel_create;    /* hooked when a channel record is
                                       initially created. */
        event_t *channel_destroy;   /* same, but for destruction. */
        event_t        *channel_add;            /* hooked when a client joins a channel */
        event_t        *channel_del;            /* same, but with part */

        event_t        *server_introduce;  /* called when server_introduce() is
                                       called, the server's structure is passed
                                       as the data */
        event_t        *server_establish;  /* called during the misc phase of
                                       server_establish(), extra data that
                                       might need to be sent should be sent
                                       here! */

        event_t        *can_join_channel;  /* called when the can_join_channel()
                                       function is called.  see channel.h */
        event_t        *can_see_channel;   /* called when the can_see_channel()
                                       function is called.  see channel.h */
        event_t        *can_send_channel;  /* called when the can_send_channel()
                                       function is called.  see channel.h */
        event_t        *can_nick_channel;  /* called when the can_nick_channel()
                                       function is called.  see channel.h */

        event_t *can_send_client;   /* called when the can_send_client()
                                       function is called.  see client.h */
        event_t *can_nick_client;   /* called when the can_nick_client()
                                       function is called.  see client.h */
    } events;

    struct {
        /* while there are three connection stages, the last one has no list
         * as it is very brief, clients who would pass stage3 are in the, you
         * guessed it, client list */
        LIST_HEAD(, connection) *stage1;
        LIST_HEAD(, connection) *stage2;

        LIST_HEAD(, connection) *clients;

        /* server connections are here, so they aren't handled with client
         * connections (which are (assumed)stages 1/2, and (known) stage3) */
        LIST_HEAD(, connection) *servers;
    } connections;

    /* things in here define the size of their respective hashes */
    struct {
        hashtable_t *client;
        hashtable_t *client_history;
        hashtable_t *command;
        hashtable_t *channel;
    } hashes;

    struct {
        struct mdext_header *channel;
        struct mdext_header *class;
        struct mdext_header *client;
    } mdext;

    struct {
        unsigned char nick[256];
        unsigned char nick_first[256];
        unsigned char channel[256];
        unsigned char host[256];
    } maps;

    struct {
        LIST_HEAD(, message_set) *sets; /* our list of message sets */
        message_t *msgs; /* array of different messages */
        int        count;
        int        size; /* size of the 'msgs' array */
    } messages;

    struct {
        struct send_flag *flags;    /* array of 'send_flag' structures */
        int size;                    /* size of the array */

        int ops;                    /* +o users */
        int servmsg;                    /* +s users */
    } sflag;

    struct {
        LIST_HEAD(, privilege_set) *sets; /* our list of privilege sets */
        privilege_set_t *oper_set;  /* default operator privileges */
        privilege_t *privs;            /* array of privileges */
        int        count;
        int        size;                    /* size of the privs array */

        /* actual privileges created by the daemon are here */
        int        priv_operator;            /* the 'operator' privilege, allows
                                       changing of the 'o' usermode */
        int        priv_shs;            /* this privilege allows users to see
                                       information about hidden servers */
        int        priv_srch;            /* this privilege allows users to see the
                                       original host of users (if it has been
                                       changed) */
#define CAN_SEE_SERVER(cli, srv)                                        \
 (srv->flags & IRCD_SERVER_HIDDEN ? BPRIV(cli, ircd.privileges.priv_shs) : 1)
#define CAN_SEE_REAL_HOST(cli, target)                                        \
 (cli == target || BPRIV(cli, ircd.privileges.priv_srch))

    } privileges;

    protocol_t *default_proto;

    /* lists of stuff! */
    struct {
        struct isocket_list *listeners;
        LIST_HEAD(, server) *servers;
        LIST_HEAD(, server_connect) *server_connects;
        LIST_HEAD(, client) *clients;
        struct client_history_list *client_history;
        LIST_HEAD(, class) *classes;
        LIST_HEAD(, protocol) *protocols;
        LIST_HEAD(, channel) *channels;
        LIST_HEAD(, command) *commands;
        LIST_HEAD(, isupport) *isupport;
        LIST_HEAD(, hostlist) *hostlists;
        LIST_HEAD(, xinfo_handler) *xinfo_handlers;
        LIST_HEAD(, xattr_handler) *xattr;
    } lists;
} ircd;

/* don't forget to set our name correctly */
#undef LOG_MODULENAME
#define LOG_MODULENAME "ircd"

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
