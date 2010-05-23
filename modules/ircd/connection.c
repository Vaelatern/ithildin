/*
 * connection.c: connection handling routines
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains routines for accepting/checking/otherwise handling raw
 * socket connections.  Once connections are accepted they are passed to the
 * protocol system for further work.  This file also handles sending of data
 * using the queue mechanism, and other trivial socket related functions.
 */

#include <ithildin/stand.h>

/* for non-blocking dns and ident */
#include "../dns/dns.h"
#include "../dns/lookup.h"
#include "../ident/ident.h"

#include "ircd.h"

IDSTRING(rcsid, "$Id: connection.c 831 2009-02-09 00:42:56Z wd $");

static void connection_stage2_done(connection_t *);
static void connection_init_lookups(connection_t *);

/* set the protocol for a connection, possibly cleaning up if necessary for
 * protocol changes */
void set_connection_protocol(connection_t *cp, protocol_t *proto) {

    if (cp->proto != NULL)
        clear_connection_objects(cp);
    
    cp->proto = proto;
    cp->proto->setup(cp);
}

/* safely remove both client and server objects from the connection */
void clear_connection_objects(connection_t *cp) {
    client_t *cli = cp->cli;
    server_t *srv = cp->srv;

    if (cli != NULL) {
        cli->conn = NULL;
        cp->cli = NULL;
        destroy_client(cli, "");
    }

    if (srv != NULL) {
        srv->conn = NULL;
        cp->srv = NULL;
        destroy_server(srv, "");
    }
}

void destroy_connection(connection_t *c, char *reason) {
    char msg[512];

    /* destroy their client/server entries. */
#ifdef DEBUG_CODE
    if (c->cli != NULL && c->srv != NULL)
        log_warn("connection %p has non-null client *and* server entry", c);
#endif
    /* destroy_client/server will set the cli/srv entries to NULL, then call us
     * again to really destroy the connection.  this makes it safe to call
     * 'destroy_connection' even if you're not sure what kind of connection you
     * have. */
    if (c->cli != NULL) {
        destroy_client(c->cli, reason);
        return;
    } else if (c->srv != NULL) {
        destroy_server(c->srv, reason);
        return;
    }

    /* Try to send them an error message if we can.  We assume someone has
     * tried to flush their sendq or doesn't think we should, so if it's
     * empty we send an error, otherwise we clean it off and don't bother
     * trying to send since other sends have already failed (and we infer
     * that this means the socket is not writeable) */
    if (STAILQ_EMPTY(&c->sendq) && reason != NULL && *reason != '\0') {
        snprintf(msg, 512, "\r\nERROR :Closing Link: %s (%s)\r\n", 
                c->host, reason);
        socket_write(c->sock, msg, strlen(msg));
    } else {
        while (STAILQ_FIRST(&c->sendq) != NULL)
            sendq_pop(c);
    }

    if (c->pass != NULL)
        free(c->pass);

    if (c->buf != NULL)
        free(c->buf);

    destroy_socket(c->sock);
    del_from_class(c);
    LIST_REMOVE(c, lp);
    free(c);
}

/* this function closes all 'unknown' (stage1/stage2) connections, handy in
 * various places. */
int close_unknown_connections(char *reason) {
    connection_t *cp;
    int count = 0;

    while (!LIST_EMPTY(ircd.connections.stage1)) {
        cp = LIST_FIRST(ircd.connections.stage1);
        destroy_connection(cp, reason);
        count++;
    }
    while (!LIST_EMPTY(ircd.connections.stage2)) {
        cp = LIST_FIRST(ircd.connections.stage2);
        destroy_connection(cp, reason);
        count++;
    }

    return count;
}

/* this function attempts to flush the send queue of a connection.  it returns
 * 1 if the client still exists (is not sendq'd off), or 0 otherwise. */
int sendq_flush(connection_t *conn) {
    struct sendq_item *sip;
    int ret;

    /* if the connection is writeable and has a send queue, push things off to
     * the socket. */
    if (conn->flags & IRCD_CONNFL_WRITEABLE) {
        while ((sip = STAILQ_FIRST(&conn->sendq)) != NULL) {
            ret = socket_write(conn->sock, sip->block->msg + sip->offset,
                    sip->block->len - sip->offset);
            if (ret <= 0)
                break;
            else if ((size_t)ret != sip->block->len - sip->offset) {
                sip->offset += ret;
                break;
            } else {
                conn->stats.sent += sip->block->len;
                conn->stats.psent++;
                sendq_pop(conn);
            }
        }
    }
    if (conn->flags & IRCD_CONNFL_NOSENDQ && conn->sendq_items == 0)
        conn->flags &= ~IRCD_CONNFL_NOSENDQ;
    else if (!(conn->flags & IRCD_CONNFL_NOSENDQ) &&
            conn->sendq_items > conn->cls->sendq) {
        destroy_connection(conn, "SendQ Exceeded");
        return 0;
    }
    return 1;
}

HOOK_FUNCTION(ircd_listen_hook) {
    connection_t *c = NULL;
    isocket_t *sp = NULL;
    isocket_t *listener = (isocket_t *)data;
    void **returns;
    int x;
    int dead;

    while ((sp = socket_accept(listener)) != NULL) {
        /* create a new connection, and immediately begin host and ident
         * checks */
        c = calloc(1, sizeof(connection_t));
        c->sock = sp;
        c->signon = c->last = me.now;
        sp->udata = c;
        get_socket_address(isock_raddr(sp), c->host, HOSTLEN + 1, NULL);

        add_to_class(LIST_FIRST(ircd.lists.classes), c);
        LIST_INSERT_HEAD(ircd.connections.stage1, c, lp);

        /* check for stage 1 access */
        returns = hook_event(ircd.events.stage1_connect, c);
        x = 0;
        dead = 0;
        while (x < hook_num_returns) {
            if (returns[x] != NULL) {
                /* this is not an allowed connection, drop it with the given
                 * error message. */
                destroy_connection(c, returns[x]);
                dead = 1;
                break;
            }
            x++;
        }

        if (dead)
            continue; /* connection was dropped above */

        add_hook(sp->datahook, ircd_connection_datahook);

        /* success?  do auth-stuff */
#ifdef HAVE_OPENSSL
        if (SOCKET_SSL(listener)) {
            if (socket_ssl_enable(c->sock)) {
                if (socket_ssl_accept(c->sock)) {
                    if (!SOCKET_SSL_HANDSHAKING(c->sock))
                        connection_init_lookups(c); /* handshake is done? */
                    else
                        c->flags |= IRCD_CONNFL_SSLINIT;
                    continue; /* handshake isn't done. */
                }
            }
            /* otherwise some error occured */
            destroy_connection(c, "");
            continue;
        }
#endif

        connection_init_lookups(c);
    }

    return NULL;
}

/* this is used to initialize the stage2 lookups.  it is kept separate so that
 * SSL accepts can be done prior to the data sending. */
static void connection_init_lookups(connection_t *c) {
    char msg[256];

    if (!(c->flags & IRCD_CONNFL_DNS)) {
        sprintf(msg, ":%s NOTICE AUTH :*** Looking up your hostname...\r\n",
                ircd.me->name);
        socket_write(c->sock, msg, strlen(msg));
        dns_lookup(DNS_C_IN, DNS_T_PTR, c->host, connection_lookup_hook);
    }
    if (!(c->flags & IRCD_CONNFL_IDENT)) {
        sprintf(msg, ":%s NOTICE AUTH :*** Checking Ident...\r\n",
                ircd.me->name);
        socket_write(c->sock, msg, strlen(msg));
        check_ident(c->sock, connection_ident_hook);
    }
    if (IRCD_CONN_DONE(c) && IRCD_CONN_NEED_STAGE2(c))
        connection_stage2_done(c);
}

/* this function handles receiving/sending of lookups.  the first time it is
 * called for a socket it will either have a successful ptr lookup or a
 * failure.  if the ptr lookup is successful it then performs a host lookup on
 * the host returned.  if that lookup succeeds the connection's hostname is set
 * to that, otherwise in any case of failure the connection's hostname is set
 * to its IP address */
HOOK_FUNCTION(connection_lookup_hook) {
    dns_lookup_t *dlp = (dns_lookup_t *)data;
    struct dns_rr *drp;
    connection_t *c, *c2;
    char msg[256];
    char ip[FQDN_MAXLEN];

    c = LIST_FIRST(ircd.connections.stage1);
    while (c != NULL) {
        c2 = LIST_NEXT(c, lp);

        if (dlp->type == DNS_T_PTR) {
            /* this was a reverse lookup.  skip hosts doing address lookups and
             * non-matching connections. */
            if (c->flags & IRCD_CONNFL_DNS || strcasecmp(c->host, dlp->data)) {
                c = c2;
                continue;
            }

            /* this connection matches.  look for a ptr record. */
            drp = LIST_FIRST(&dlp->rrs.an);
            while (drp != NULL) {
                /* Use the first PTR answer we get. */
                if (drp->type == DNS_T_PTR)
                    break;
                drp = LIST_NEXT(drp, lp);
            }
            if (dlp->flags & DNS_LOOKUP_FL_FAILED || drp == NULL) {
                sprintf(msg, ":%s NOTICE AUTH :*** Couldn't find your "
                        "hostname.\r\n", ircd.me->name);
                socket_write(c->sock, msg, strlen(msg));
                c->flags |= IRCD_CONNFL_DNS;
            } else {
                strlcpy(c->host, drp->rdata.txt, HOSTLEN + 1);
                c->flags |= IRCD_CONNFL_DNS_PTR;
                dns_lookup(DNS_C_IN, (c->sock->peeraddr.family == PF_INET6
                            ? DNS_T_AAAA : DNS_T_A), c->host,
                        connection_lookup_hook);
            }
        } else {
            dns_type_t atype;

            /* this was a forward lookup.  skip reverses and non-matching
             * connections as above. */
            if (!(c->flags & IRCD_CONNFL_DNS) ||
                    strcasecmp(c->host, dlp->data)) {
                c = c2;
                continue;
            }

            get_socket_address(isock_raddr(c->sock), ip, FQDN_MAXLEN, NULL);
            /* a match.  look for the right A or AAAA record.  there may, in
             * this case, be several of them. */
            atype = (c->sock->peeraddr.family == PF_INET6 ?
                    DNS_T_AAAA : DNS_T_A);
            drp = LIST_FIRST(&dlp->rrs.an);
            while (drp != NULL) {
                /* Check each answer.. */
                if (drp->type == atype && 
                    drp->rdlen > 0 && drp->rdata.txt != NULL &&
                    !strcasecmp(drp->rdata.txt, ip))
                    break;
                drp = LIST_NEXT(drp, lp);
            }
            if (dlp->flags & DNS_LOOKUP_FL_FAILED || drp == NULL) {
                sprintf(msg, ":%s NOTICE AUTH :*** Couldn't find your "
                        "hostname.\r\n", ircd.me->name);
                socket_write(c->sock, msg, strlen(msg));
                c->flags |= IRCD_CONNFL_DNS;
                strcpy(c->host, ip);
            } else {
                sprintf(msg, ":%s NOTICE AUTH :*** Found your hostname.\r\n",
                        ircd.me->name);
                if (!istr_okay(ircd.maps.host, c->host)) {
                    sprintf(msg, ":%s NOTICE AUTH :*** Found your hostname, "
                            "but it contains invalid characters.  Using IP "
                            "instead.\r\n", ircd.me->name);
                    /* log a warning, too */
                    log_warn("bad hostname from %s: %s", ip, c->host);
                    strcpy(c->host, ip);
                }
                socket_write(c->sock, msg, strlen(msg));
                c->flags |= IRCD_CONNFL_DNS_ADDR;
            }
        }
        if (IRCD_CONN_DONE(c) && IRCD_CONN_NEED_STAGE2(c))
            connection_stage2_done(c);

        c = c2;
    }

    return NULL;
}

HOOK_FUNCTION(connection_ident_hook) {
    struct ident_request *i = (struct ident_request *)data;
    connection_t *c;
    char msg[256];

    /* try and find our socket. */
    LIST_FOREACH(c, ircd.connections.stage1, lp) {
        if (!memcmp(isock_laddr(c->sock), &i->laddr,
                    sizeof(struct isock_address)) &&
                !memcmp(isock_raddr(c->sock), &i->raddr,
                    sizeof(struct isock_address)))
            break; /* found it */
    }
    if (c == NULL)
        return NULL; /* didn't find it. */

    c->flags |= IRCD_CONNFL_IDENT;
    if (!strcmp(i->answer, "")) {
        strcpy(c->user, "~");
        sprintf(msg, ":%s NOTICE AUTH :*** No Ident response.\r\n",
                ircd.me->name);
    } else {
        strncpy(c->user, i->answer, USERLEN);
        sprintf(msg, ":%s NOTICE AUTH :*** Got Ident response.\r\n",
                ircd.me->name);
    }

    socket_write(c->sock, msg, strlen(msg));
    if (IRCD_CONN_DONE(c) && IRCD_CONN_NEED_STAGE2(c))
        connection_stage2_done(c);

    return NULL;
}

static void connection_stage2_done(connection_t *c) {
    void **returns;
    int x = 0;

    returns = hook_event(ircd.events.stage2_connect, c);
    while (x < hook_num_returns) {
        if (returns[x] != NULL) {
            /* give the user an uninteresting error message, then dump our
             * structure. */
            destroy_connection(c, (char *)returns[x]);
            return;
        }
        x++;
    }
    LIST_REMOVE(c, lp);
    LIST_INSERT_HEAD(ircd.connections.stage2, c, lp);

    /* now stick them in the default protocol */
    set_connection_protocol(c, ircd.default_proto);
    c->last = me.now;
    /* and monitor them */
    socket_monitor(c->sock, SOCKET_FL_READ|SOCKET_FL_WRITE);
}

HOOK_FUNCTION(ircd_connection_datahook) {
    isocket_t *s = (isocket_t *)data;
    connection_t *c = (connection_t *)s->udata;
    char msg[512];
    int ret;

#ifdef HAVE_OPENSSL
    if (c->flags & IRCD_CONNFL_SSLINIT) {
        if (SOCKET_ERROR(s)) {
            destroy_connection(c, "");
            return NULL;
        }
        c->flags &= ~IRCD_CONNFL_SSLINIT;
        connection_init_lookups(c);
        return NULL;
    } else if (!IRCD_CONN_DONE(c)) {
        log_debug("!IRCD_CONN_DONE(%p) caught! (flags %x)", c, c->flags);
        return NULL; /* ignore unfinished connections */
    }
#endif
            
    /*
     * Read first, then write, then check errors.  If the read succeeds
     * (does not return IRCD_CONNECTION_CLOSED) we should be ensured that
     * the connection is still valid.
     */

    if (SOCKET_READ(s)) {
        ret = (int)c->proto->input(ep, data);
        /* We do this in a while loop because we may end up doing more than
         * one proto change (unlikely but the support for this is nearly
         * free so...) */
        while (ret == IRCD_PROTOCOL_CHANGED) {
            /* A protocol change implies two things to us:
             * 1) We may need to resize the buffer for this protocol.  We
             * can only resize up, not down!  In the down case we do the
             * wrong thing and close the connection instead of looping a
             * parser to clear the buffer. (XXX: worth fixing?)
             * 2) If there is any data in the buffer we should call the new
             * protocol's input function, protocol changes may leave
             * lingering data which would otherwise generate socket events
             * if we acted on it, so.. act on it here. */

            if (c->bufsize < c->proto->bufsize) {
                /* We must resize the buffer... we can happily just use
                 * realloc here, since nothing should depend on the address
                 * of c->buf between input calls */
                c->buf = realloc(c->buf, c->proto->bufsize);
                c->bufsize = c->proto->bufsize;
            }
            if (c->buflen > 0)
                /* We pass NULL as the event parameter for the input
                 * function as a way of saying "this was forcefully called
                 * outisde the normal event mechanism".  This is to tell the
                 * protocol handler that it should try to flush its current
                 * buffer even if there is no data from the socket.
                 * XXX: this is kind of a hack... */
                ret = (int)c->proto->input(NULL, data);
            else
                ret = 0; /* kill the loop :) */
        }

        if (ret == IRCD_CONNECTION_CLOSED)
            return NULL; /* nothing to do... */
    }

    if (SOCKET_WRITE(s))
        /* Only flag that we are writeable.  The writer hook takes care of
         * doing buffered writes for us outside the construct of this loop.
         * I think, potentially falsely, that it may be beneficial to batch
         * writes and reads in this manner. */
        c->flags |= IRCD_CONNFL_WRITEABLE;

    if (SOCKET_ERROR(s)) {
        if (c->srv != NULL) {
            sprintf(msg, "Read error from %s.  Closing link (%s)",
                    c->srv->name, socket_strerror(s));
            sendto_flag(SFLAG("GNOTICE"), msg);
            sendto_serv_butone(c->srv, NULL, ircd.me, NULL, "GNOTICE", ":%s",
                    msg);
        }
        /* XXX: Maybe swap in destroy_server and destroy_connection after it
         * with a source set on destroy_server of the server we're
         * destroying (so we don't send it a message for no reason) */
        destroy_connection(c, (s->err != 0 ? strerror(s->err) :
                    "Client closed connection"));
        return NULL;
    }

    return NULL;
}

HOOK_FUNCTION(ircd_writer_hook) {
    connection_t *cp, *cp2;

    /* handle sendqs for each of our three connection stages, in order */
    cp = LIST_FIRST(ircd.connections.stage1);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        sendq_flush(cp);
        cp = cp2;
    }
    cp = LIST_FIRST(ircd.connections.stage2);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        sendq_flush(cp);
        cp = cp2;
    }
    cp = LIST_FIRST(ircd.connections.clients);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        sendq_flush(cp);
        cp = cp2;
    }

    /* servers too */
    cp = LIST_FIRST(ircd.connections.servers);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        sendq_flush(cp);
        cp = cp2;
    }

    return NULL;
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
