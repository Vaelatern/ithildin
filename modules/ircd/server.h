/*
 * server.h: support structures/prototypes for server.e
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: server.h 703 2006-03-02 13:06:55Z wd $
 */

#ifndef IRCD_SERVER_H
#define IRCD_SERVER_H

struct server {
    char    name[SERVLEN + 1];        /* server's name */
    char    info[GCOSLEN + 1];
    int            hops;                /* number of hops from us */

    /* check to see if this is our server */
#define MYSERVER(srv) (srv->conn != NULL)
    connection_t *conn;                /* the connection for this server, if any */

    conf_list_t *conf;                /* the configuration data for this server, if
                                   any.  */
    struct server *parent;        /* parent server.  NULL if this is our own
                                   server structure (in the ircd uberstruct) */
        
#define IRCD_SERVER_INTRODUCED        0x0001        /* defined if we have introduced
                                           ourselves (sent PASS/SERVER/...) */
#define IRCD_SERVER_REGISTERED        0x0010        /* defined if the server is registered
                                           with us.  this is only relevant for
                                           local connections! */
#define SERVER_REGISTERED(srv) (srv->flags & IRCD_SERVER_REGISTERED)
#define IRCD_SERVER_CONNECTING        0x0020        /* set if we are currently trying to
                                           connect to this server .. */
#ifdef HAVE_OPENSSL
#define IRCD_SERVER_SSLINIT        0x0040        /* set if we are doing an SSL handshake
                                           with the server */
#endif
#define IRCD_SERVER_SENTREG        0x0080        /* defined if we sent a registration
                                           request to this server (useful for
                                           /connect and friends */
#define IRCD_SERVER_SYNCHED(srv) !(srv->flags & IRCD_SERVER_BURSTING)
#define IRCD_SERVER_BNICKCHAN        0x0100        /* set if we've sent our nick/chan
                                           burst. */
#define IRCD_SERVER_BMISC        0x0200        /* set if we've sent our miscellaneous
                                           data burst */
#define IRCD_SERVER_BURSTING        0x0400        /* set if we're bursting (set when
                                           server_establish is called for the
                                           first time, and unset at the end of
                                           it all. */
#define SERVER_HUB(srv) (srv->flags & IRCD_SERVER_HUB)
#define IRCD_SERVER_HUB                0x00010000 /* if this server is a hub, set
                                                this */

#define SERVER_HIDDEN(srv) (srv->flags & IRCD_SERVER_HIDDEN)
#define IRCD_SERVER_HIDDEN        0x00020000 /* hide this server? */

/* the second one is just a handy shortcut for clients.  we don't technically
 * know anything about the client structure here, but.. */
#define SERVER_MASTER(srv) (srv->flags & IRCD_SERVER_MASTER)
#define CLIENT_MASTER(cli) (cli->server->flags & IRCD_SERVER_MASTER)
#define IRCD_SERVER_MASTER        0x00040000 /* a 'master' server (formerly
                                                known as a U:lined server). */
    int            flags;                        /* various server flags */

/* store protocol capability flags here.  these aren't stored with the protocol
 * because they're sent via the CAPAB command. :(  protocols can specify
 * defaults though, for their support. */
#define SERVER_SUPPORTS(srv, flag) ((srv)->pflags & (flag))
    int            pflags;

    LIST_ENTRY(server) lp;
};

/* this structure is used to specify connectory for servers.  typically used
 * with auto-connects. */
struct server_connect {
    char    *name;                        /* the server's name */
    conf_list_t *conf;                        /* conf describing the connect */
    server_t *srv;                        /* the server structure undergoing
                                           connection.  usually NULL */
    time_t  interval;                        /* interval to connect at */
    time_t  last;                        /* when this server was last
                                           connected/connected to */

    LIST_ENTRY(server_connect) lp;
};

server_t *create_server(connection_t *);
void destroy_server(server_t *, char *);
server_t *find_server(char *);

void server_set_flags(server_t *);

void server_introduce(server_t *);
int server_establish(server_t *);
void server_register(server_t *);

struct server_connect *create_server_connect(char *);
struct server_connect *find_server_connect(char *);
void destroy_server_connect(struct server_connect *);
int server_connect(struct server_connect *, char *);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
