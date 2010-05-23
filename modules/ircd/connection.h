/*
 * connection.h: support structures/prototypes for connection.c
 * 
 * Copyright 2002-2006 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: connection.h 831 2009-02-09 00:42:56Z wd $
 */

#ifndef IRCD_CONNECTION_H
#define IRCD_CONNECTION_H

struct connection {
    isocket_t *sock;                /* our socket */
    client_t *cli;                  /* either a client or a server, should */
    server_t *srv;                  /* never be both! */
    char    host[HOSTLEN + 1];      /* user and host data (as given by
                                       identd/dns) */
    char    user[USERLEN + 1];
    char    *pass;                  /* password, if not NULL it must be
                                       malloc'd memory.  It will be
                                       destroyed after client registration.  */

    char    *buf;                   /* temporary buffer pointer, possibly
                                       used in many places */
    size_t  buflen;                 /* length of data in the buffer */
    size_t  bufsize;                /* size of the temporary buffer. */
    size_t  bufresize;              /* When non-0 a resize request for the
                                       buffer.  We do this in the socket
                                       event hook, after reading has
                                       completed. */

    class_t   *cls;                 /* our connection class */
    protocol_t *proto;              /* our current protocol */
    message_set_t *mset;            /* our message set, NULL for servers */

    time_t  signon;                 /* time of connection (not registration) */
    time_t  last;                   /* last update to this item */
    int     flood;                  /* flood level (clients only) */

    struct {
        int64_t sent;               /* bytes sent */
        int64_t psent;              /* "packets" sent */
        int64_t recv;               /* bytes received */
        int64_t precv;              /* "packets" received */
    } stats;

#define IRCD_CONNFL_DNS_PTR         0x1
#define IRCD_CONNFL_DNS_ADDR        0x2
#define IRCD_CONNFL_DNS            (IRCD_CONNFL_DNS_PTR | IRCD_CONNFL_DNS_ADDR)
#define IRCD_CONNFL_IDENT           0x4
#define IRCD_CONNFL_STAGE2          0x8
#define IRCD_CONN_DONE(x)                                                     \
    (((x)->flags & (IRCD_CONNFL_DNS | IRCD_CONNFL_IDENT)) ==                  \
     (IRCD_CONNFL_DNS | IRCD_CONNFL_IDENT))
#define IRCD_CONN_NEED_STAGE2(x) (!((x)->flags & IRCD_CONNFL_STAGE2))

#define IRCD_CONNFL_WRITEABLE       0x100 /* set if the socket is writeable */
#define CONN_PINGSENT(conn) (conn->flags & IRCD_CONNFL_PINGSENT)
#define IRCD_CONNFL_PINGSENT        0x200 /* set when a PING is sent to test
                                             for activity */
        
#define IRCD_CONNFL_NOSENDQ         0x400 /* this is set if we don't want to
                                             observe send queue limits on the
                                             connection (mostly useful for
                                             sending synchronization data to
                                             servers).  it is not unset until
                                             sendq drops to 0. */
#ifdef HAVE_OPENSSL
#define IRCD_CONNFL_SSLINIT         0x800 /* set when we're waiting for the
                                             first data event for an initiating
                                             SSL socket.  when we get this
                                             event we can begin the regular
                                             connection initiation. */
#endif
#define IRCD_CONNFL_DIRTYBUFFER    0x1000 /* set when the contents of the
                                             buffer are 'dirty' (typically
                                             an overflow command which must
                                             be discarded) */

    int     flags;                  /* connection flags (DO NOT PUT
                                       CLIENT/SERVER FLAGS HERE) */

    int     sendq_items;            /* items on the send queue */
    STAILQ_HEAD(, sendq_item) sendq;/* and te queue itself */
    LIST_ENTRY(connection) lp;
};

void set_connection_protocol(connection_t *, protocol_t *);
void clear_connection_objects(connection_t *);
void destroy_connection(connection_t *, char *);
int close_unknown_connections(char *);
int sendq_flush(connection_t *);

HOOK_FUNCTION(connection_lookup_hook);
HOOK_FUNCTION(connection_ident_hook);
HOOK_FUNCTION(ircd_connection_datahook);
HOOK_FUNCTION(ircd_writer_hook);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
