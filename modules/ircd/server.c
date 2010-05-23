/*
 * server.c: server handling functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains functions to handle adding/removing servers, and to
 * connect to them and parse connects from them.  It also contains synch
 * functions (which may need to be worked over at some point or otherwise
 * augmented).
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: server.c 832 2009-02-22 00:50:59Z wd $");

/* extra prototypes */
static void server_sendnicks(server_t *, server_t *);
static void server_sendchans(server_t *);

/* this function creates a new server and links it to the given connection, if
 * any is specified. */
server_t *create_server(connection_t *conn) {
    server_t *sp = malloc(sizeof(server_t));
    memset(sp, 0, sizeof(server_t));
    sp->conn = conn;

    if (conn != NULL) {
        conn->srv = sp;
        sp->pflags = conn->proto->flags;
    }

    return sp;
}

/* this function removes the given server from the network, and destroys its
 * connection if it is connected to us.  It also takes care of propogating
 * quits for its clients out to users, and sending the split message.  it calls
 * itself recursively to remove other servers connected to the server it is
 * called for, as well.  We do not take care of sending a SQUIT message to
 * srv!  This is important, because some callers (specifically the SQUIT
 * command code) may very well need to pass on a SQUIT if this isn't a
 * locally connected server.  In this case they are expected to do so, we
 * don't do it here because we don't have the information required. */
static int destroy_server_count = 0;
static char destroy_server_splitmsg[512];
void destroy_server(server_t *srv, char *msg) {
    client_t *cp, *cp2;
    server_t *sp, *sp2;
        
    /* if this is our initial call, create the split message and send a SQUIT
     * back to any other servers behind us. */
    destroy_server_count++;
    if (destroy_server_count == 1)
        sprintf(destroy_server_splitmsg, "%s %s", srv->parent->name, srv->name);

    if (SERVER_REGISTERED(srv)) {
        /* First remove all its attached servers, this runs recursively and
         * will send out SQUITs and QUITs and all that gack for servers that
         * are NOQUIT-dumb */
        sp = LIST_FIRST(ircd.lists.servers);
        while (sp != NULL) {
            sp2 = LIST_NEXT(sp, lp);
            if (sp->parent == srv)
                destroy_server(sp, msg);
            sp = sp2;
        }

        /* Now remove all the clients from this server */
        cp = LIST_FIRST(ircd.lists.clients);
        while (cp != NULL) {
            cp2 = LIST_NEXT(cp, lp);
            if (cp->server == srv) {
                cp->flags |= IRCD_CLIENT_KILLED;
                /* Send out QUITs for servers that are not 'NOQUIT' enabled.
                 * In doing so we send QUITs to *everything* that doesn't do
                 * NOQUIT, including the source of the SQUIT! The only place
                 * we don't send towards is srv, since we believe this will
                 * be handled properly downstream.  That is, NOQUIT makes
                 * the strange assumption that every server in the path of a
                 * SQUIT knows all about the server being squit, but other
                 * servers not in the path might not.  Why/how this makes
                 * sense is a mystery to me, and this is *really* spammy. */
                if (CLIENT_REGISTERED(cp))
                    sendto_serv_pflag_butone(PROTOCOL_SFL_NOQUIT, false, srv,
                            cp, NULL, NULL, "QUIT", ":%s",
                            destroy_server_splitmsg);
                destroy_client(cp, destroy_server_splitmsg);
            }
            cp = cp2;
        }

        ircd.stats.servers--;
        if (srv->conn != NULL)
            ircd.stats.serv.servers--;

        /* Send the SQUIT for this server, last but not least. */
        /* Some servers are really dumb.  For servers which properly support
         * NOQUIT we can simply send a single SQUIT for the first-tier
         * server.  For other servers (dreamforge is stupid like this) we
         * have to send a SQUIT for *every single server* that goes away,
         * and we do it in the same fashion as when sending out QUITs, that
         * is, everything but srv will get a SQUIT, even if it might not
         * need it (see above commentary) */
        if (destroy_server_count == 1)
            sendto_serv_butone(srv, NULL, NULL, srv->name, "SQUIT",
                    ":%s", ircd.me->name);
        else
            sendto_serv_pflag_butone(PROTOCOL_SFL_NOQUIT, false, srv,
                    NULL, NULL, srv->name, "SQUIT", ":%s", ircd.me->name);

        /* Lastly, remove 'srv' from the list of servers */
        LIST_REMOVE(srv, lp);
    }

    /* If this server is ours we have to close the connection.  We try first
     * to flush the sendq.  If sendq_flush returns 0 it has closed the
     * connection for us (socket error), otherwise we close it ourself. */
    if (MYSERVER(srv)) {
        srv->conn->srv = NULL;
        if (sendq_flush(srv->conn))
            destroy_connection(srv->conn, msg);
    }

    free(srv);
    destroy_server_count--;
}

/* This finds a server, but unlike other find_* functions, uses match() instead
 * of a hash lookup or strcmp.  This allows callers to have servernames passed
 * as masks, and save a little typing. */
server_t *find_server(char *name) {
    server_t *sp;

    if (match(name, ircd.me->name))
        return ircd.me; /* it's us */

    LIST_FOREACH(sp, ircd.lists.servers, lp) {
        if (match(name, sp->name))
            return sp;
    }

    return NULL;
}

/* this function looks at the conf for a server, and sets any special flags
 * that may be available in that conf on the server.  some flags (hidden and
 * master) are recursive, and must be set if the server's parent posesses them
 * too. */
void server_set_flags(server_t *srv) {

    if (srv->conf == NULL)
        return;

    if (conf_find_entry("hub", srv->conf, 1) != NULL)
        srv->flags |= IRCD_SERVER_HUB; /* they hub something.. */
    else
        srv->flags &= ~IRCD_SERVER_HUB;
    if (str_conv_bool(conf_find_entry("master", srv->conf, 1), 0) ||
            SERVER_MASTER(srv->parent))
        srv->flags |= IRCD_SERVER_MASTER; /* they're a "master" server */
    else
        srv->flags &= ~IRCD_SERVER_MASTER;
    if (str_conv_bool(conf_find_entry("hidden", srv->conf, 1), 0) ||
            SERVER_HIDDEN(srv->parent))
        srv->flags |= IRCD_SERVER_HIDDEN; /* they're secret/hidden. */
    else
        srv->flags &= ~IRCD_SERVER_HIDDEN;
}

/* this function is called to introduce ourselves to the specified server.  we
 * expect that the server has an associated conf entry.  we send a protocol
 * message, followed by our password and other information. */
void server_introduce(server_t *srv) {
    char *pass;
    char *s;
    class_t *cls = NULL;

    s = conf_find_entry("class", srv->conf, 1);
    if (s != NULL)
        cls = find_class(s);
    if (cls == NULL) {
        log_warn("server %s does not have a connection class, using default",
                srv->name);
        cls = LIST_FIRST(ircd.lists.classes);
    }
    add_to_class(cls, srv->conn);

    s = conf_find_entry("protocol", srv->conf, 1);
    /* XXX: we should perform more stringement protocol checks. */
    sendto_serv_from(srv, NULL, NULL, NULL, "PROTOCOL", "%s",
            s == NULL ?  "ithildin1" : s);

    /* send them our password unless we're using SSL for the server, in which
     * case this is not necessary. */
#ifdef HAVE_OPENSSL
    if (!SOCKET_SSL(srv->conn->sock))
#endif
    {
        /* send them our pass, possibly encrypted */
        if ((pass = conf_find_entry("ourpass", srv->conf, 1)) == NULL) {
            pass = ""; /* this isn't good */
            log_warn("no password for server %s, trying a blank one.",
                    srv->name);
        }

        sendto_serv_from(srv, NULL, NULL, NULL, "PASS", "%s :TS", pass);
    }

    /* If it's a TS server send 'SVINFO' here.  We assume TS3, and require that
     * as the minimum version. */
    if (SERVER_SUPPORTS(srv, PROTOCOL_SFL_TS))
        sendto_serv_from(srv, NULL, NULL, NULL, "SVINFO", "3 3 0 :%d", me.now);
    /* let the extra functions make their calls here, before we formally
     * introduce ourselves. */
    hook_event(ircd.events.server_introduce, srv);
    sendto_serv_from(srv, NULL, NULL, NULL, "SERVER", "%s 1 :%s",
            ircd.me->name, ircd.me->info);
    sendto_serv_from(srv, NULL, NULL, NULL, "PING", ":%s", ircd.me->name);

    /* and we're introduced!  also, we'll be bursting from this point .. */
    srv->flags |= (IRCD_SERVER_BURSTING | IRCD_SERVER_INTRODUCED);
}

/* this function is called twice.  once to burst nicknames and channels, and
 * again to burst ancillary data to the server.  it should be called externally
 * (probably by the 'PONG' command) until the server is synched. */
int server_establish(server_t *srv) {
    log_debug("server_establish called");

    if (!(srv->flags & IRCD_SERVER_BNICKCHAN)) {
        /* send out an 'established' notice. */
        sendto_flag(SFLAG("GNOTICE"),
                "Link with %s established, states:%s%s", srv->name,
                SERVER_MASTER(srv) ? " Master" : "",
                SERVER_HIDDEN(srv) ? " Hidden" : "");
        sendto_serv_butone(srv, NULL, ircd.me, NULL, "GNOTICE",
                ":Link with %s established: TS link", srv->name);
        srv->conn->flags |= IRCD_CONNFL_NOSENDQ;
        /* all the work for this function is done by 'server_sendnicks',
         * defined below.  just call it for ourself and let it do all the work.
         * we have to pass 'srv' so it knows who to send to and what server not
         * to establish! */
        server_sendnicks(srv, ircd.me);
        server_sendchans(srv);
        sendto_serv_from(srv, NULL, NULL, NULL, "PING", ":%s", ircd.me->name);
        sendto_flag(SFLAG("GNOTICE"),
                "Sent nick/channel burst to %s (%d messages)", srv->name,
                srv->conn->sendq_items);
        srv->flags |= IRCD_SERVER_BNICKCHAN;
        return 1;
    } else if (!(srv->flags & IRCD_SERVER_BMISC)) {
        /* burst other stuff */
        hook_event(ircd.events.server_establish, srv);
        sendto_flag(SFLAG("GNOTICE"),
                "%s has synched nick/channel burst, sending miscellaneous "
                "burst (%d messages).  synch time %d %s.",
                srv->name, srv->conn->sendq_items, me.now - srv->conn->signon,
                (me.now - srv->conn->signon != 1 ? "secs" : "sec"));
        srv->flags |= IRCD_SERVER_BMISC;
        return 1;
    } else if (srv->flags & IRCD_SERVER_BURSTING) {
        /* they've finished. */
        sendto_flag(SFLAG("GNOTICE"),
                "%s has synched all bursts.  synch time %d %s.", srv->name,
                me.now - srv->conn->signon,
                (me.now - srv->conn->signon != 1 ? "secs" : "sec"));
        srv->flags &= ~IRCD_SERVER_BURSTING;
        srv->conn->flags &= ~IRCD_CONNFL_NOSENDQ;
        return 1;
    } else
        return 0;
}

/* this function is a lot like client_register().  It takes a filled in server
 * structure and 'registers' it on the network.  It also takes care of saying
 * the server is registered, and may do some other stuff too. */
void server_register(server_t *sp) {
    server_t *uplink = sp->parent;

    /* right now just send it in the other direction.. */
    sendto_serv_butone(sp, NULL, uplink, NULL, "SERVER", "%s %d :%s", sp->name,
            sp->hops + 1, sp->info);

    LIST_INSERT_HEAD(ircd.lists.servers, sp, lp);

    ircd.stats.servers++;
    if (sp->conn != NULL) {
        ircd.stats.serv.servers++;
        LIST_REMOVE(sp->conn, lp);
        LIST_INSERT_HEAD(ircd.connections.servers, sp->conn, lp);
        
        /* clean out the password area */
        if (sp->conn->pass != NULL) {
            free(sp->conn->pass);
            sp->conn->pass = NULL;
        }
    }

    sp->flags |= IRCD_SERVER_REGISTERED;
}

/* this sends nicks in a recursive fashion.  given a server to start from,
 * find all servers whose parents are 'from' and call server_sendnicks() for
 * them (which will recurse until you hit the leaves).  do not send data for
 * 'to', of course */
void server_sendnicks(server_t *to, server_t *from) {
    server_t *sp;
    client_t *cp;

    if (!MYSERVER(to)) {
        log_warn("server_establish(%s) called with non-local server as dest",
                to->name);
        return;
    }

    /* send data for ourself, if 'from' == ircd.me (us), don't send a SERVER
     * command, otherwise, introduce the server and all users on that server */
    if (from != ircd.me)
        sendto_serv_from(to, NULL, from->parent, NULL, "SERVER", "%s %d :%s",
                from->name, from->hops, from->info);
    LIST_FOREACH(cp, ircd.lists.clients, lp) {
        if (!CLIENT_REGISTERED(cp) || cp->server != from)
            continue;
        to->conn->proto->register_user(to->conn, cp);
    }
    /* then introduce all the servers on that from, and all their users, and
     * all the servers on that server, ... */
    LIST_FOREACH(sp, ircd.lists.servers, lp) {
        if (sp->parent == from && sp != to)
            server_sendnicks(to, sp);
    }
}

/* this sends our current list of channels to the specified server.  we are
 * careful to only send info from our side of the network, as well. */
void server_sendchans(server_t *to) {
    channel_t *chp;

    LIST_FOREACH(chp, ircd.lists.channels, lp)
        to->conn->proto->sync_channel(to->conn, chp);
}

/* server connectory stuff lives below here.  we keep our own internal list of
 * servers for which we are making outgoing connections in this file, and we
 * register our own hooks etc for them. */
HOOK_FUNCTION(server_connecting_hook);

/* this function creates a new server connection structure, it is probably only
 * going to be called from ircd_parse_connects() in conf.c */
struct server_connect *create_server_connect(char *name) {
    struct server_connect *scp;

    scp = malloc(sizeof(struct server_connect));
    memset(scp, 0, sizeof(struct server_connect));
    scp->name = strdup(name);

    LIST_INSERT_HEAD(ircd.lists.server_connects, scp, lp);

    return scp;
}

struct server_connect *find_server_connect(char *name) {
    struct server_connect *scp;

    LIST_FOREACH(scp, ircd.lists.server_connects, lp) {
        /* do match, actually. */
        if (match(name, scp->name))
            return scp;
    }

    return NULL;
}

void destroy_server_connect(struct server_connect *scp) {

    LIST_REMOVE(scp, lp);
    free(scp->name);
    free(scp);
}

/* this function attempts to make a connection to the server described in the
 * conf entry.  It sets up a socket, and places a special hook on it, then
 * tries to make a connection.  We return 1 if the initial connect was
 * successful, or 0 otherwise. */
int server_connect(struct server_connect *scp, char *uport) {
    conf_list_t *clp = scp->conf;
    connection_t *cp;
    isocket_t *isp;
    char *host, *port, *s;
    protocol_t *proto;
    server_t *sp;

    /* be sure to update the attempt time. */
    scp->last = me.now;

    /* do some error checking here */
#define SCONF_CHECK(_name)                                                    \
    s = conf_find_entry(#_name, clp, 1);                                      \
    if (s == NULL) {                                                          \
        log_warn("no " #_name " entry for server %s.  cannot connect.",       \
                scp->name);                                                   \
        return 0;                                                             \
    }
    
    SCONF_CHECK(protocol);
    if ((proto = find_protocol(s)) == NULL) {
        log_warn("protocol %s is not loaded.  cannot connect to %s.", s,
                scp->name);
        return 0;
    }
    SCONF_CHECK(address);

    if ((isp = create_socket()) == NULL) {
        log_warn("couldn't create socket to connect to %s", scp->name);
        return 0;
    }
    /* if they specify a host to connect from, use that.. if they don't we just
     * assume they want to use the bound address of the server.  the reason we
     * allow a 'connectfrom' entry is for hosts in mixed IPv4/IPv6 environments
     * who may by default bind to an IPv4 address but want to connect to an
     * IPv6 server. */
    host = conf_find_entry("connect-from", clp, 1);
    if (host == NULL)
        host = ircd.address;
    if (!set_socket_address(isock_laddr(isp), host, NULL, SOCK_STREAM)) {
        log_warn("unable to set socket address %s to connect to server %s",
                host, scp->name);
        destroy_socket(isp);
        return 0;
    }
    if (!open_socket(isp)) {
        log_warn("unable to open socket to connect to server %s",
                scp->name);
        destroy_socket(isp);
        return 0;
    }

    /* first try and create the outbound connection to the host .. */
    host = conf_find_entry("address", clp, 1);
    if (host == NULL) {
        log_warn("server conf %s has no address.", scp->name);
        destroy_socket(isp);
        return 0;
    }
    if (uport != NULL)
        port = uport;
    else {
        port = conf_find_entry("port", clp, 1);
        if (port == NULL) {
            log_warn("server conf %s has no port.", scp->name);
            port = "6667"; /* UGH. :) */
        }
    }
    if (!socket_connect(isp, host, port, SOCK_STREAM)) {
        log_warn("socket_connect(%s, %s) failed for server %s", host, port,
                scp->name);
        destroy_socket(isp);
        return 0;
    }
    /* we monitor it for data and hook the 'server_connecting_hook' function,
     * which handles whatever might happen. */
    socket_monitor(isp, SOCKET_FL_READ|SOCKET_FL_WRITE);
    add_hook(isp->datahook, server_connecting_hook);
    
    /* well, we connected, now create a connection to hold this socket, and
     * fill in some of the data.. */
    cp = calloc(1, sizeof(connection_t));

    cp->sock = isp;
    get_socket_address(isock_raddr(isp), cp->host, HOSTLEN + 1, NULL);
    strcpy(cp->user, "<unknown>");
    cp->cls = LIST_FIRST(ircd.lists.classes); /* stick them in the default class
                                            temporarily. */
    cp->signon = me.now;
    cp->last = me.now;
    set_connection_protocol(cp, proto);

    /* put it on the stage2 list.  technically it should stay as stage2 until
     * the negotiation is finished and server_register() is called. */
    LIST_INSERT_HEAD(ircd.connections.stage2, cp, lp);

    sp = cp->srv;
    /* now just fill in the server structure.. */
    strlcpy(sp->name, scp->name, SERVLEN + 1);
    sp->conf = clp;
    sp->parent = ircd.me;
    sp->flags = IRCD_SERVER_CONNECTING;
    
    /* now point scp->srv at the new server structure, and point isp->udata at
     * scp so that when server_connecting_hook gets hooked it will get all
     * necessary data. :) */
    scp->srv = sp;
    isp->udata = scp;

    return 1; /* whew! all done. */
}

/* called when a server connection is finished.  as long as the socket isn't
 * errored, we just call server_introduce and wait for the server to respond
 * with its own introduction. */
HOOK_FUNCTION(server_connecting_hook) {
    isocket_t *sock = (isocket_t *)data;
    struct server_connect *scp = (struct server_connect *)sock->udata;
    server_t *sp = scp->srv;
    connection_t *cp = scp->srv->conn;

    /* check conditions.  the only two we care about are the error condition,
     * and the writeable condition.  if this function gets hooked it means the
     * connect is done and all that. :) */
    if (SOCKET_ERROR(sock)) {
        /* sigh.  failed connect, there's a whole lot of cleaning up that needs
         * to be done, now. */
        sendto_flag(ircd.sflag.ops, "connection to server %s failed (%s)",
                sp->name, socket_strerror(sock));
        sendto_serv_butone(NULL, NULL, ircd.me, NULL, "GNOTICE",
                "connection to server %s failed (%s)", sp->name,
                socket_strerror(sock));
        destroy_server(sp, "");
        scp->srv = NULL; /* don't forget to unset this */

        return NULL;
    }
    if (SOCKET_WRITE(sock))
        cp->flags |= IRCD_CONNFL_WRITEABLE;

#ifdef HAVE_OPENSSL
    if (SOCKET_SSL(sock)) {
        /* if the socket is already set for SSL and we got hooked that means
         * the SSL handshake has completed!  send a status message and continue
         * the negotiation. */
        sendto_flag(SFLAG("GNOTICE"), "SSL handshake with server %s complete",
            sp->name);
        sp->flags &= ~IRCD_SERVER_SSLINIT;
    } else if (str_conv_bool(conf_find_entry("ssl", sp->conf, 1), 0)) {
        /* they want SSL.  Start doing SSL handshaking on this socket. */
        if (socket_ssl_enable(sock)) {
            if (socket_ssl_connect(sock)) {
                if (SOCKET_SSL_HANDSHAKING(sock)) {
                    sp->flags |= IRCD_SERVER_SSLINIT;
                    return NULL; /* wait for handshake to finish */
                }
            }
        }
        /* if an error occured in any of the above, or if the handshake
         * completed, we fall through below to try and continue or rescue the
         * connection. */
    }
#endif

    /* assume it's connected.  shuffle some stuff around, and clean
     * up our structures */
    remove_hook(sock->datahook, server_connecting_hook);
    sock->udata = sp->conn; /* set udata to what is expected */
    /* also, make sure IRCD_CONN_DONE() will test positive */
    cp->flags |= (IRCD_CONNFL_DNS | IRCD_CONNFL_IDENT);
    add_hook(sock->datahook, ircd_connection_datahook);
    scp->srv = NULL; /* all done */
    sendto_flag(SFLAG("GNOTICE"), "Connection to server %s established",
            sp->name);
    server_introduce(sp);

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
