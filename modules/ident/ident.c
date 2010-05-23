/*
 * ident.c: ident (rfc1413) module.
 * 
 * Copyright 2002, 2003 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * The ident module performs an ident (RFC1413) check on the socket given to
 * it, and uses a callback hook to return the results.  See below for furhter
 * documentation.
 */

#include <ithildin/stand.h>

#include "ident.h"

IDSTRING(rcsid, "$Id: ident.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");

/* provide rfc1413-style identification services */
LIST_HEAD(, ident_request) ident_requests;

static struct ident_request *create_ident_request(isocket_t *);
static void destroy_ident_request(struct ident_request *);

HOOK_FUNCTION(ident_socket_hook);
HOOK_FUNCTION(ident_timer_hook);

/* this will allocate an ident request and fill it in as much as possible */
static struct ident_request *create_ident_request(isocket_t *sock) {
    struct ident_request *irp = malloc(sizeof(struct ident_request));
    
    memset(irp, 0, sizeof(struct ident_request));

    irp->timer = TIMER_INVALID;
    memcpy(&irp->laddr, isock_laddr(sock), sizeof(struct isock_address));
    memcpy(&irp->raddr, isock_raddr(sock), sizeof(struct isock_address));
    LIST_INSERT_HEAD(&ident_requests, irp, lp);

    return irp;
}

/* this destroys an ident request.  as long as func is not NULL it will call
 * the function with whatever it thinks the answer is at the time. */
static void destroy_ident_request(struct ident_request *irp) {

    if (irp->func != NULL)
        irp->func(NULL, irp);
    if (irp->timer != TIMER_INVALID)
        destroy_timer(irp->timer);
    if (irp->sock != NULL)
        destroy_socket(irp->sock);
    LIST_REMOVE(irp, lp);
    free(irp);
}

/* this is much like the nbdns lookup function.  the user gives us a socket
 * to perform an ident request on, and we do this.  they also provide a
 * callback function (of type 'hook_function') which is called with the
 * filled out 'ident_req' structure.  The function should look at the laddr and
 * raddr members of the ident_req structure given back, and compare them to all
 * sockets which they have pending in order to find a match. */
void check_ident(isocket_t *sock, hook_function_t func) {
    struct ident_request *irp = create_ident_request(sock);
    char ourhost[FQDN_MAXLEN];

    irp->func = func;
    if ((irp->sock = create_socket()) == NULL) {
        log_warn("unable to create socket for ident check");
        destroy_ident_request(irp);
        return;
    }

    /* make sure we query from the same source! */
    get_socket_address(isock_laddr(sock), ourhost, FQDN_MAXLEN, NULL);
    if (!set_socket_address(isock_laddr(irp->sock), ourhost, NULL,
                SOCK_STREAM)) {
        log_warn("unable to set socket address for ident check");
        destroy_ident_request(irp);
        return;
    }
    if (!open_socket(irp->sock)) {
        log_warn("unable to open socket for ident check");
        destroy_ident_request(irp);
        return;
    }

    /* now connect to their 'auth' port */
    get_socket_address(isock_raddr(sock), ourhost, FQDN_MAXLEN, NULL);
    if (!socket_connect(irp->sock, ourhost, "113", SOCK_STREAM)) {
        /* no ident available */
        destroy_ident_request(irp);
        return;
    }
    irp->sock->udata = irp;

    /* now monitor the socket, attach our parsing hook, and let the system do
     * the work until data comes back */
    socket_monitor(irp->sock, SOCKET_FL_READ | SOCKET_FL_WRITE);
    add_hook(irp->sock->datahook, ident_socket_hook);
    irp->timer = create_timer(0, IDENT_TIMEOUT, ident_timer_hook, irp);
}

/* this function immediately cancels all ident lookups hooked to the given
 * function without calling that function back. */
void ident_cancel(hook_function_t func) {
    struct ident_request *ip, *ip2;

    ip = LIST_FIRST(&ident_requests);
    while (ip != NULL) {
        ip2 = LIST_NEXT(ip, lp);
        if (ip->func == func) {
            ip->func = NULL;
            destroy_ident_request(ip);
        }
        ip = ip2;
    }
}

#define IDENT_BUFLEN 512
HOOK_FUNCTION(ident_socket_hook) {
    isocket_t *sock = (isocket_t *)data;
    struct ident_request *irp = (struct ident_request *)sock->udata;
    char buf[IDENT_BUFLEN], *s, *reply, *os, *userid = "";
    int lport, rport;
    int len;

    /* if our socket is writeable, send our request and return, waiting for
     * results to come back. */
    if (SOCKET_WRITE(sock)) {
        /* whee, now send our request... */
        get_socket_address(&irp->laddr, NULL, 0, &lport);
        get_socket_address(&irp->raddr, NULL, 0, &rport);
        sprintf(buf, "%d , %d\r\n", rport, lport);
        /* for this exercise, we assume that the operating system's buffers will
         * cover for the very brief transactions.  this should be a perfectly
         * safe assumption for just about any modern OS */
        if (!socket_write(irp->sock, buf, strlen(buf))) {
            /* of course, if this proves wrong... */
            get_socket_address(&irp->raddr, buf, IDENT_BUFLEN, NULL);
            log_warn("couldn't write to auth sock on %s", buf);
            destroy_ident_request(irp);
            return NULL;
        }

        /* we no longer care if we can write to it */
        socket_unmonitor(sock, SOCKET_FL_WRITE);
        return NULL;
    } else if (SOCKET_READ(sock)) {
        /* otherwise, assume that our socket is readable and read from it.
         * we're * only going to try to read once, so if the entire
         *  (typically less than 100 character) message doesn't arrive all
         *  at once, too bad */
        len = socket_read(sock, buf, IDENT_BUFLEN - 1);
        if (!len) /* no data, don't kill the lookup for that though */
            return NULL;
        else if (len < 0) {
            log_debug("ident socket error: %s", socket_strerror(sock));
            destroy_ident_request(irp);
            return NULL;
        } else if (len < 16) {
            /* the reply cannot possibly be shorter than this and be valid */
            log_debug("ident protocol error: %s", socket_strerror(sock));
            destroy_ident_request(irp);
            return NULL;
        }

        if (!(buf[len - 2] == '\r' && buf[len - 1] == '\n')) {
            /* bogus ident packet */
            destroy_ident_request(irp);
            return NULL;
        }
        buf[len - 2] = '\0'; /* just to be sure */
                
        /* do/while so we break out of the loop */
        do {
            s = strchr(buf, ':');
            if (s == NULL)
                break;
            s++;
            while (isspace(*s))
                s++;
            reply = s;
            if (strncmp(reply, "USERID", 6))
                break;
        
            s = strchr(reply, ':');
            if (s == NULL)
                break;
            s++;
            while (isspace(*s))
                s++;
            os = s;
            /* accept UNIX and OTHER.  Are there others?  Should we just accept
             * everything? */
            if (strncmp(os, "UNIX", 4) && strncmp(os, "OTHER", 6))
                break;

            s = strchr(os, ':');
            if (s == NULL)
                break;
            s++;
            while (isspace(*s))
                s++;
            userid = s;
        } while (0);

        strncpy(irp->answer, userid, IDENT_MAXLEN);
        destroy_ident_request(irp);
        return NULL;
    } else if (SOCKET_ERROR(sock)) {
        /* if our socket broke (for whatever reason) drop the whole matter */
        log_notice("error on socket %d", sock->fd);
        destroy_ident_request(irp);
        return NULL;
    }

    return NULL;
}

HOOK_FUNCTION(ident_timer_hook) {
    struct ident_request *irp = (struct ident_request *)data;

    irp->timer = TIMER_INVALID;
    destroy_ident_request(irp);
    return NULL;
}

MODULE_UNLOADER(ident) {
    struct ident_request *irp;

    while ((irp = LIST_FIRST(&ident_requests)) != NULL)
        destroy_ident_request(irp);
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
