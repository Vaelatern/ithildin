/*
 * helloworld.c: helloworld module code
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * The helloworld module is reeally just a demonstration module.  It accepts
 * connections on a specified port, and performs dns and ident checks on the
 * connecting host.  It reports this info to the host, and then says (in a
 * cheerful manner): Hello, World! to them.  It is really intended as a simple
 * demonstration system.
 */

#include <ithildin/stand.h>
#include "../dns/dns.h"
#include "../dns/lookup.h"
#include "../ident/ident.h"

IDSTRING(rcsid, "$Id: helloworld.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ident dns
*/

LIST_HEAD(, hw_connection) hw_connlist;
struct hw_connection {
    isocket_t *sock; /* our socket */

#define HW_FL_LOOKUP   0x1
#define HW_FL_RESOLVED 0x2
#define HW_FL_IDENTED  0x4
#define CONN_DONE(conn) (((conn)->flags & HW_FL_RESOLVED) && ((conn)->flags & HW_FL_IDENTED))
    int            flags;

    char    username[IDENT_MAXLEN + 1];
    char    hostname[HOSTNAME_MAXLEN + 1];
    char    host[HOSTNAME_MAXLEN + 1];

    LIST_ENTRY(hw_connection) lp;
};

HOOK_FUNCTION(hw_listener_hook);
HOOK_FUNCTION(hw_lookup_hook);
HOOK_FUNCTION(hw_ident_hook);
void hw_finish_conn(struct hw_connection *conn);

isocket_t *listensocket;

MODULE_LOADER(helloworld) {
    conf_list_t *conf = *confdata;
    char *s;
    char *port = "6667";
    char *addr = (char *)"0.0.0.0"; /* any address */
    int gsa_port;
    char gsa_host[FQDN_MAXLEN + 1];

    s = conf_find_entry("port", conf, 1);
    if (s != NULL)
        port = s;
    s = conf_find_entry("address", conf, 1);
    if (s != NULL)
        addr = s;

    listensocket = create_socket();
    if (listensocket == NULL) {
        log_error("couldn't create listener");
        return 0;
    }
    if (!set_socket_address(isock_laddr(listensocket), addr, port,
                SOCK_STREAM)) {
        log_error("couldn't set address/port %s/%s for listener", addr, port);
        return 0;
    }
    if (!open_socket(listensocket)) {
        log_error("couldn't open listener");
        return 0;
    }
    if (!socket_listen(listensocket)) {
        log_error("couldn't listen on listener!");
        return 0;
    }

    /* well with that all done, we set our handler and let the server wait
     * for activity. */
    add_hook(listensocket->datahook, hw_listener_hook);
    socket_monitor(listensocket, SOCKET_FL_READ);

    get_socket_address(isock_laddr(listensocket), gsa_host, FQDN_MAXLEN + 1,
            &gsa_port);
    log_notice("listening for activity on %s/%d", gsa_host, gsa_port);

    return 1;
}

MODULE_UNLOADER(helloworld) {
    destroy_socket(listensocket);
}

HOOK_FUNCTION(hw_listener_hook) {
    char msg[256];
    int slen;

    /* data is not used here, if we are hooked we can basically assume a new
     * connection is ready (or several new connections are ready) */
    isocket_t *newsp;
    while ((newsp = socket_accept(listensocket)) != NULL) {
        struct hw_connection *conn = malloc(sizeof(struct hw_connection));
        memset(conn, 0, sizeof(struct hw_connection));

        conn->sock = newsp;
        LIST_INSERT_HEAD(&hw_connlist, conn, lp);
        conn->sock->udata = conn;
        conn->flags = 0;

        get_socket_address(isock_raddr(conn->sock), conn->host,
                HOSTNAME_MAXLEN + 1, NULL);

        slen = sprintf(msg, "Looking up your IP. (%s)\n", conn->host);
        socket_write(conn->sock, msg, slen);
        dns_lookup(DNS_C_IN, DNS_T_PTR, conn->host, hw_lookup_hook);

        socket_write(conn->sock, "Checking ident.\n", 16);
        check_ident(conn->sock, hw_ident_hook);
    }
    return NULL;
}

HOOK_FUNCTION(hw_lookup_hook) {
    dns_lookup_t *dlp = (dns_lookup_t *)data;
    struct hw_connection *c, *c2;
    struct dns_rr *drp;
    char msg[256];
    char ip[FQDN_MAXLEN + 1];
    int slen;

    /* find our sappy connection */
    c = LIST_FIRST(&hw_connlist);
    while (c != NULL) {
        c2 = LIST_NEXT(c, lp);

        if (dlp->type == DNS_T_PTR) {
            /* this was a reverse lookup.  skip hosts doing address lookups and
             * non-matching connections. */
            if (c->flags & HW_FL_RESOLVED || strcasecmp(c->host, dlp->data)) {
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
                slen = sprintf(msg, "Couldn't resolve your IP.\n");
                socket_write(c->sock, msg, slen);
                strlcpy(c->hostname, c->host, HOSTNAME_MAXLEN + 1);
                c->flags &= ~HW_FL_LOOKUP;
                c->flags |= HW_FL_RESOLVED;
            } else {
                slen = sprintf(msg, "Your IP resolved to %s. Checking if this "
                    "resolves to %s.\n", drp->rdata.txt, c->host);
                socket_write(c->sock, msg, slen);
                strlcpy(c->host, drp->rdata.txt, HOSTNAME_MAXLEN + 1);
                c->flags |= HW_FL_LOOKUP;
                dns_lookup(DNS_C_IN, (c->sock->peeraddr.family == PF_INET6
                            ? DNS_T_AAAA : DNS_T_A), c->host, hw_lookup_hook);
            }
        } else {
            dns_type_t atype;

            /* this was a forward lookup.  skip reverses and non-matching
             * connections as above. */
            if (!(c->flags & HW_FL_LOOKUP) ||
                    strcasecmp(c->host, dlp->data)) {
                c = c2;
                continue;
            }

            get_socket_address(isock_raddr(c->sock), ip, FQDN_MAXLEN + 1, NULL);
            /* a match.  look for the right A or AAAA record.  there may, in
             * this case, be several of them. */
            atype = (c->sock->peeraddr.family == PF_INET6 ?
                    DNS_T_AAAA : DNS_T_A);
            drp = LIST_FIRST(&dlp->rrs.an);
            while (drp != NULL) {
                /* Check each answer.. */
                if (drp->type == atype &&
                        !strcasecmp(drp->rdata.txt, ip))
                    break;
                drp = LIST_NEXT(drp, lp);
            }
            if (dlp->flags & DNS_LOOKUP_FL_FAILED || drp == NULL) {
                slen = sprintf(msg, "%s does not resolve back to %s.\n",
                    c->host, ip);
                socket_write(c->sock, msg, slen);
                c->flags &= ~HW_FL_LOOKUP;
                c->flags |= HW_FL_RESOLVED;
                strlcpy(c->hostname, ip, HOSTNAME_MAXLEN + 1);
            } else {
                slen = sprintf(msg, "%s does indeed resolve back to %s.\n",
                    c->host, ip);
                socket_write(c->sock, msg, slen);
                c->flags &= ~HW_FL_LOOKUP;
                c->flags |= HW_FL_RESOLVED;
                strlcpy(c->hostname, c->host, HOSTNAME_MAXLEN + 1);
            }
        }

        if (CONN_DONE(c))
            hw_finish_conn(c);

        c = c2;
    }

    return NULL;
}

HOOK_FUNCTION(hw_ident_hook) {
    struct ident_request *i = (struct ident_request *)data;
    struct hw_connection *conn;

    /* find our socket */
    conn = LIST_FIRST(&hw_connlist);
    while (conn != NULL) {
        if (!memcmp(isock_laddr(conn->sock), &i->laddr,
                    sizeof(struct isock_address)) &&
                !memcmp(isock_raddr(conn->sock), &i->raddr,
                    sizeof(struct isock_address)))
            break; /* we found our socket */
        conn = LIST_NEXT(conn, lp);
    }

    if (conn == NULL)
        return NULL; /* didn't find it. */

    conn->flags |= HW_FL_IDENTED;
    if (!strcmp(i->answer, "")) {
        strcpy(conn->username, "~unknown");
        socket_write(conn->sock, "Couldn't find your ident.\n", 26);
    }
    else {
        strcpy(conn->username, i->answer);
        socket_write(conn->sock, "Found your ident.\n", 18);
    }
        
    if (CONN_DONE(conn))
        hw_finish_conn(conn);


    return NULL;
}

void hw_finish_conn(struct hw_connection *conn) {
    char s[512];
    int slen;

    slen = sprintf(s, "Greetings %s@%s.\nI have only this to say:\n"
        "Hello, world!\n", conn->username, conn->hostname);
    socket_write(conn->sock, s, slen);
    close_socket(conn->sock);
    LIST_REMOVE(conn, lp);
    free(conn);
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
