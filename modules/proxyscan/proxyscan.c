/*
 * proxyscan.c: proxy scanning module
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module performs scans on machines to determine if they provide an open
 * proxy which can be abused.
 */

#include <ithildin/stand.h>

#include "proxyscan.h"

IDSTRING(rcsid, "$Id: proxyscan.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");

/* these two are the events we hook depending on what happens with our proxies.
 * they are declared outside the structure because they should be externally
 * used, unlike everything else. */
event_t *proxy_found;
event_t *proxy_clean;

/* we keep all the global variables in a structure named after the module,
 * hopefully this will make namespace collision less dangerous.  yuck. :) */
struct proxyscan_data {
    struct pscan_entry_list queue;  /* our queue of scans */

    int            count;                    /* number of scans going */
    int            maxcount;                    /* maximum number of scans */

    hashtable_t *hash;                    /* table of entries for fast lookup */

    char    bind[IPADDR_MAXLEN + 1];        /* address to bind connections on */
    char    target[IPADDR_MAXLEN + 1];        /* address to send connections to */
    char    *target_port;                /* port on target.. */
    char    *target_pattern;                /* pattern received from target */

    /* these are the 'scan' flags that tell us what to check for, the first
     * lists what we should check for period, the second lists scans for which
     * we should be less 'aggressive' (for example, a passive socks4 scan means
     * we only dump it if it allows a connection, and not if it gives an
     * ambiguous error message) */
    int            checks;
    int            passive;

    char    **skipaddrs;            /* addresses we won't scan */
    int            skiptabsize;            /* and the count for the array size */

    int            timeout;                    /* timeout length for socket inactivity. */
    int            cache_expire;            /* expiry time for cached entries */
} proxyscan;

#define proxy_find(name) hash_find(proxyscan.hash, name)

/* function prototypes here */
HOOK_FUNCTION(proxyscan_timer_hook);

/* functions to create/destroy proxyscan entries */
struct pscan_entry *proxyscan_create(char *);
void proxyscan_destroy(struct pscan_entry *);

/* this function wraps to start the next check, or dump the structure,
 * depending on what happens.  this should be called by all functions to
 * initiate scans (within this module, externally use proxy_scan()) */
void proxyscan_scan_next(struct pscan_entry *);

isocket_t *proxyscan_socket_connect(char *, struct pscan_entry *, char *);

/* these are the functions to begin checks */
void proxyscan_socks(struct pscan_entry *);
void proxyscan_telnet(struct pscan_entry *);
void proxyscan_http(struct pscan_entry *);
HOOK_FUNCTION(proxyscan_socks4_hook);
HOOK_FUNCTION(proxyscan_socks5_hook);
HOOK_FUNCTION(proxyscan_telnet_hook);
HOOK_FUNCTION(proxyscan_http_hook);

struct pscan_entry *proxyscan_create(char *addr) {
    struct pscan_entry *pent = malloc(sizeof(struct pscan_entry));
    struct pscan_entry *pent2;

    strcpy(pent->addr, addr);
    pent->sock = NULL;
    pent->hit = pent->last = me.now;
    pent->timer = TIMER_INVALID;
    pent->flags = 0;
    pent->udata = NULL; /* initialize for the user */
    
    /* if our cache is full, remove the last entry so we can add ours */
    if (proxyscan.count == proxyscan.maxcount) {
        /* find the oldest cached entry and expire it, or return NULL if none
         * of our entries are cached! */
        TAILQ_FOREACH_REVERSE(pent2, &proxyscan.queue, pscan_entry_list, lp) {
            if (pent2->last == 0) {
                proxyscan_destroy(pent2);
                break;
            }
        }
        if (proxyscan.count == proxyscan.maxcount) {
            /* we didn't delete anything. */
            log_warn("proxyscan: unable to create new scan.  no entries "
                    "available!  consider increasing queue size.");
            free(pent);
            return NULL;
        }
    }

    TAILQ_INSERT_HEAD(&proxyscan.queue, pent, lp);
    hash_insert(proxyscan.hash, pent);
    proxyscan.count++;

    return pent;
}

void proxyscan_destroy(struct pscan_entry *pent) {
    
    if (pent->sock != NULL)
        destroy_socket(pent->sock);
    if (pent->timer != TIMER_INVALID)
        destroy_timer(pent->timer);

    TAILQ_REMOVE(&proxyscan.queue, pent, lp);
    hash_delete(proxyscan.hash, pent);
    free(pent);

    proxyscan.count--;
}

struct pscan_entry *proxy_scan(char *addr, int flags, void *udata) {
    struct pscan_entry *pent;
    int i;

    /* see if this address is skipped */
    for (i = 0;i < proxyscan.skiptabsize;i++) {
        if (ipmatch(proxyscan.skipaddrs[i], addr)) {
            log_debug("skipping scan for address %s (matches %s)", addr,
                    proxyscan.skipaddrs[i]);
            return NULL; /* don't scan if we're supposed to skip it */
        }
    }

    pent = proxy_find(addr);
    if (pent != NULL) {
        /* if they don't want to do cacheing, be sure to also ignore any cached
         * entries! */
        if (flags & PSCAN_FL_NOCACHE) {
            proxyscan_destroy(pent);
            pent = proxyscan_create(addr);
        } else {
            TAILQ_REMOVE(&proxyscan.queue, pent, lp);
            TAILQ_INSERT_HEAD(&proxyscan.queue, pent, lp);
            if (pent->last != 0)
                return NULL; /* already scanning for it anyhow */
        }
    } else
        pent = proxyscan_create(addr);

    if (pent == NULL)
        return NULL; /* if we couldn't get a new entry */

    pent->flags |= flags & 0xFFFF0000;
    pent->udata = udata;

    /* now initialize the first lookup on it. */
    proxyscan_scan_next(pent);
    return pent;
}

/* this decides which checks to perform based on various flags, both what the
 * user wants us to check and what checks have been performed to date. */
void proxyscan_scan_next(struct pscan_entry *pent) {

    pent->hit = me.now; /* update our cache hit thingy ;) */

    /* we might be done, if we are, free our structure if need-be.  also, we
     * might have found something, and we may not be checking all open ends on
     * all systems.  if that's the case, we do the same thing here. */
    if ((pent->flags & PSCAN_FL_ALL_CHECK) == proxyscan.checks ||
            (pent->flags & PSCAN_FL_OPEN &&
             !(pent->flags & PSCAN_FL_CHECKALL))) {
        /* if the proxy isn't open we know we're done.  hook the 'proxy_clean'
         * event if the user wants it.  otherwise, if the proxy is open (we
         * know we're done whatever it is we're doing in this if block), notify
         * via the proxy_found hook. */
        if (!(pent->flags & PSCAN_FL_OPEN) && 
                pent->flags & PSCAN_FL_NOTIFY_CLEAN)
            hook_event(proxy_clean, pent);
        else if (pent->flags & PSCAN_FL_OPEN)
            hook_event(proxy_found, pent);

        if (pent->flags & PSCAN_FL_NOCACHE) {
            proxyscan_destroy(pent);
            return;
        }

        pent->flags |= PSCAN_FL_CACHE;
        pent->last = 0; /* we set 'last' to 0 to mark this as a cached entry. */
        pent->udata = NULL; /* user data is invalidated for cache entries. */
        pent->sock = NULL; /* and we ought to be all done here, too. */
        if (pent->timer == TIMER_INVALID)
            pent->timer = create_timer(0, proxyscan.cache_expire,
                    proxyscan_timer_hook, pent);
        else
            adjust_timer(pent->timer, 0, proxyscan.cache_expire);

        return;
    }

    if (pent->timer == TIMER_INVALID)
        pent->timer = create_timer(0, proxyscan.timeout, proxyscan_timer_hook,
                pent);
    else
        adjust_timer(pent->timer, 0, proxyscan.timeout);

    /* if we're not done, determine what is next on our laundry list of checks,
     * and do that. */
    if (proxyscan.checks & PSCAN_FL_SOCKS4_CHECK &&
            !(pent->flags & PSCAN_FL_SOCKS4_CHECK)) {
        proxyscan_socks(pent);
        return;
    } else if (proxyscan.checks & PSCAN_FL_SOCKS5_CHECK &&
            !(pent->flags & PSCAN_FL_SOCKS5_CHECK)) {
        proxyscan_socks(pent);
        return;
    } else if (proxyscan.checks & PSCAN_FL_TELNET_CHECK &&
            proxyscan.checks & PSCAN_FL_SOCKS5_CHECK &&
            (pent->flags & PSCAN_FL_SOCKS5_CHECK &&
             !(pent->flags & PSCAN_FL_TELNET_CHECK))) {
        /* the semantics here, though somewhat confusing, basically say: if
         * we're checking for wingates *and* socks5, *and* this entry has been
         * checked for socks5 but NOT for wingate, then we do this.  what we
         * do, is if a socks5 server has been found, we skip the wingate check
         * but set the flag.  if one hasn't been found, we check for a wingate.
         * simple enough, heh */
        if (pent->flags & PSCAN_FL_SOCKS5_FOUND) {
            pent->flags |= PSCAN_FL_TELNET_CHECK;
            proxyscan_scan_next(pent);
            return;
        } else {
            proxyscan_telnet(pent);
            return;
        }
    } else if (proxyscan.checks & PSCAN_FL_HTTP_CHECK &&
            (pent->flags & PSCAN_FL_HTTP_CHECK) != PSCAN_FL_HTTP_CHECK) {
        /* 'proxyscan_http' determines which http port needs to be checked
         * next, so just call that */
        proxyscan_http(pent);
        return;
    }

    /* we should never get here.  if we do, there's a problem */
    log_warn("proxyscan: got to end of proxyscan_scan_next!");
}
        
/* this is a handy function which performs duplicate work in
 * proxyscan_(socks|telnet|http).  it handles reporting errors, etc. */
isocket_t *proxyscan_socket_connect(char *type, struct pscan_entry *pent,
        char *port) {

    if ((pent->sock = create_socket()) == NULL) {
        log_error("could not create socket for %s proxy scan on %s",
                type, pent->addr);
        proxyscan_scan_next(pent);
        return NULL;
    }
    if (!set_socket_address(isock_laddr(pent->sock), proxyscan.bind, NULL,
                SOCK_STREAM)) {
        log_error("could not bind to address %s for %s proxy scan on %s",
                proxyscan.bind, type, pent->addr);
        destroy_socket(pent->sock);
        proxyscan_scan_next(pent);
        return NULL;
    }
    if (!open_socket(pent->sock)) {
        log_error("could not open socket for %s proxy scan on %s",
                type, pent->addr);
        destroy_socket(pent->sock);
        proxyscan_scan_next(pent);
        return NULL;
    }
    if (!socket_connect(pent->sock, pent->addr, port, SOCK_STREAM)) {
        log_error("could not open connection for %s proxy scan on %s.%s",
                type, pent->addr, port);
        destroy_socket(pent->sock);
        proxyscan_scan_next(pent);
        return NULL;
    }

    return pent->sock;
}

/******************************************************************************
 * socks4/5 stuff here
 ******************************************************************************/
#define SOCKS_PORT "socks"
void proxyscan_socks(struct pscan_entry *pent) {
    char check;

    /* flag our socks check.  might be 4 or 5. */
    if (!(pent->flags & PSCAN_FL_SOCKS4_CHECK)) {
        pent->flags |= PSCAN_FL_SOCKS4_CHECK;
        check = '4';
    } else {
        pent->flags |= PSCAN_FL_SOCKS5_CHECK;
        check = '5';
    }

    if (proxyscan_socket_connect((check == '4' ? "socks4" : "socks5"), pent,
                SOCKS_PORT))
        return; /* give up. */

    pent->sock->udata = pent; /* point back to our pent */
    socket_monitor(pent->sock, SOCKET_FL_READ|SOCKET_FL_WRITE);

    /* add our hook as necessary.  */
    if (check == '5')
        add_hook(pent->sock->datahook, proxyscan_socks5_hook);
    else
        add_hook(pent->sock->datahook, proxyscan_socks4_hook);
    
    pent->last = me.now;
}

HOOK_FUNCTION(proxyscan_socks4_hook) {
    isocket_t *sock = (isocket_t *)data;
    struct pscan_entry *pent = sock->udata;
    char request[9] = {0x04, 0x01, 0x1A, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00};
    char rcvbuf[8];
    int rcvlen;

    /* update our 'last' thingy */
    pent->last = me.now;

    /* if we're writeable, send a socks4 request.  the request looks like this:
     * 04 01 1A 0b 00 00 00 00 00.  Any kind of reply warrants treating this as
     * open, because socks4 is not specific about why it is denying the
     * request.  UNLESS 'socks4_aggressive' is set off in the conf, that is */
    if (SOCKET_WRITE(sock)) {
        if (!socket_write(sock, request, 9)) {
            /* hrm, this is bad.  if we couldn't write assume it's not an open
             * proxy (hm!) and go down to the end of the function, where the
             * next request will be performed. */
            log_debug("socks4: couldn't write request to %d[%s]", sock->fd,
                    pent->addr);
            /* in this case, we assume that port 1080 is hosed.  Skip the
             * second connection if we get ECONNREFUSED or ENETUNREACH or
             * EPIPE. */
            switch (sock->err) {
                case ECONNREFUSED:
                case ENETUNREACH:
                case EPIPE:
                    pent->flags |= PSCAN_FL_SOCKS5_CHECK;
                    break;
            }
            destroy_socket(sock);
            proxyscan_scan_next(pent);
            return NULL;
        }
        log_debug("socks4: sent request to %d[%s]", sock->fd,
                pent->addr);
        return NULL;
    }
    if (SOCKET_READ(sock)) {
        rcvlen = socket_read(sock, rcvbuf, 8);
        if (rcvlen == 0)
            return NULL; /* keep waiting */
        else if (rcvlen != 8) {
            log_debug("socks4: got rcvlen of %d for %d[%s]", rcvlen, sock->fd,
                    pent->addr);
            destroy_socket(sock); /* it's definitely not a socks4 server */
            proxyscan_scan_next(pent);
            return NULL;
        }
        log_debug("socks4: got response from %d[%s]: %.2x%.2x", sock->fd,
                pent->addr, rcvbuf[0], rcvbuf[1]);
        /* okay, got rcvbuf filled out.  let's see what we get */
        if (rcvbuf[0] != 0x00) {
            destroy_socket(sock); /* not a socks4 server */
            proxyscan_scan_next(pent);
            return NULL;
        }
        if (rcvbuf[1] == 0x5a) {
            /* request granted, this server is open */
            destroy_socket(sock);

            /* we found something. */
            pent->flags |= PSCAN_FL_SOCKS4_FOUND;
            proxyscan_scan_next(pent);
            return NULL;
        } else if (!(proxyscan.passive & PSCAN_FL_SOCKS4_CHECK)) {
            /* if we're not passive, and we get any other 'denied' reply,
             * simply say it's open.  The reason we do this is because the
             * replies do not make it clear whether the request would be
             * allowed or not for some other condition (for example, if the
             * socks4 server is full, it won't actually say so, it will simply
             * deny you.  this makes it easy to fill socks4 servers, then use
             * them as open proxies. */
            if (rcvbuf[1] == 0x5b || rcvbuf[1] == 0x5c || rcvbuf[1] == 0x5d) {
                destroy_socket(sock);

                pent->flags |= PSCAN_FL_SOCKS4_FOUND;
                proxyscan_scan_next(pent);
                return NULL;
            }
        }
        destroy_socket(sock);
        proxyscan_scan_next(pent);
        return NULL;
    }
    if (SOCKET_ERROR(sock)) {
        log_debug("socks4: error on %d[%s]", sock->fd,
                pent->addr);
        destroy_socket(sock);
        proxyscan_scan_next(pent);
        return NULL;
    }

    /* we should never get here... */
    log_warn("socks4: got to end of socks4 hook!");
    return NULL;
}

HOOK_FUNCTION(proxyscan_socks5_hook) {
    isocket_t *sock = (isocket_t *)data;
    struct pscan_entry *pent = sock->udata;
    char request[3] = {0x05, 0x01, 0x00};
    char rcvbuf[2];
    int rcvlen;

    /* update our last hit time */
    pent->last = me.now;

    /* if we're writing, send our request.  this is defined above, and in
     * RFC1928.  We only want to do unauthenticated connecting. */
    if (SOCKET_WRITE(sock)) {
        if (!socket_write(sock, request, 3)) {
            /* hrm, this is bad.  if we couldn't write assume it's not an open
             * proxy (hm!) and go down to the end of the function, where the
             * next request will be performed. */
            log_debug("socks5: couldn't write request to %d[%s]", sock->fd,
                    pent->addr);
            destroy_socket(sock);
            proxyscan_scan_next(pent);
            return NULL;
        }
        log_debug("socks5: sent request to %d[%s]", sock->fd,
                pent->addr);
        return NULL;
    }
    if (SOCKET_READ(sock)) {
        rcvlen = socket_read(sock, rcvbuf, 2);
        if (rcvlen == 0)
            return NULL; /* keep waiting */
        else if (rcvlen != 2) {
            log_debug("socks5: got rcvlen of %d for %d[%s]", rcvlen, sock->fd,
                    pent->addr);
            destroy_socket(sock); /* it's definitely not a socks5 server */
            proxyscan_scan_next(pent);
            return NULL;
        }
        log_debug("socks5: got response from %d[%s]: %.2x%.2x", sock->fd,
                pent->addr, rcvbuf[0], rcvbuf[1]);
        /* okay, got rcvbuf filled out.  let's see what we get */
        if (rcvbuf[0] != 0x05) {
            destroy_socket(sock); /* not a socks5 server */
            proxyscan_scan_next(pent);
            return NULL;
        }
        if (rcvbuf[1] == 0x00) {
            /* request granted, this server is open */
            destroy_socket(sock);

            /* we found something. */
            pent->flags |= PSCAN_FL_SOCKS5_FOUND;
            proxyscan_scan_next(pent);
            return NULL;
        }
    }
    if (SOCKET_ERROR(sock)) {
        log_debug("error on socket %d for socks5 check on %s", sock->fd,
                pent->addr);
        destroy_socket(sock);
        proxyscan_scan_next(pent);
        return NULL;
    }

    /* we should never get here... */
    log_warn("socks5: got to end of socks5 hook!");
    return NULL;
}

/******************************************************************************
 * telnet stuff here
 ******************************************************************************/
#define TELNET_PORT "telnet"
void proxyscan_telnet(struct pscan_entry *pent) {

    pent->flags |= PSCAN_FL_TELNET_CHECK;

    if (proxyscan_socket_connect("telnet", pent, TELNET_PORT) == NULL)
        return; /* no go.. */

    pent->sock->udata = pent; /* point back to our pent */
    socket_monitor(pent->sock, SOCKET_FL_READ|SOCKET_FL_WRITE);

    add_hook(pent->sock->datahook, proxyscan_telnet_hook);
    pent->last = me.now;
}

HOOK_FUNCTION(proxyscan_telnet_hook) {
    isocket_t *sock = (isocket_t *)data;
    struct pscan_entry *pent = sock->udata;
    char rcvbuf[128];
    int rcvlen;

    /* update our last hit time */
    pent->last = me.now;

    /* send a newline to prompt for a wingate reply, and then we can just wait
     * for data.  very handy. */
    if (SOCKET_WRITE(sock)) {
        if (!socket_write(sock, "\r\n", 2)) {
            /* hrm, this is bad.  if we couldn't write assume it's not an open
             * proxy (hm!) and go down to the end of the function, where the
             * next request will be performed. */
            log_debug("telnet: couldn't write request to %d[%s]", sock->fd,
                    pent->addr);
            destroy_socket(sock);
            proxyscan_scan_next(pent);
            return NULL;
        }
        log_debug("telnet: sent nudge to %d[%s]", sock->fd,
                pent->addr);
        return NULL;
    }
    if (SOCKET_READ(sock)) {
        rcvlen = socket_read(sock, rcvbuf, 127);
        if (rcvlen == 0)
            return NULL; /* keep waiting */
        else if (rcvlen < 0) {
            log_debug("telnet: socket error %s on %d[%s]",
                    socket_strerror(sock), sock->fd, pent->addr);
            destroy_socket(sock); /* it's definitely not a socks4 server */
            proxyscan_scan_next(pent);
            return NULL;
        }
        if (rcvlen != 128)
            rcvbuf[rcvlen] = '\0';
        else
            rcvbuf[127] = '\0';
        if (rcvlen == 12 && rcvbuf[0] == -1)
            return NULL; /* this is a telnet header thingy.  skip it. XXX:
                            I don't actually have any idea how this works. :) */

        log_debug("telnet: got response from %d[%s]: %d %s", sock->fd,
                pent->addr, rcvlen, rcvbuf);
        if ((rcvlen >= 8 && !strncasecmp(rcvbuf, "WinGate>", 8)) ||
                (rcvlen >= 41 && !strncasecmp(rcvbuf,
                    "\r\n\r\nUser Access Verification\r\n\r\nPassword:", 41))) {
            /* open wingate or cisco router. */
            destroy_socket(sock);

            /* another winner! */
            pent->flags |= PSCAN_FL_TELNET_FOUND;
            proxyscan_scan_next(pent);
            return NULL;
        } else {
            destroy_socket(sock);
            proxyscan_scan_next(pent);
            return NULL;
        }
    }
    if (SOCKET_ERROR(sock)) {
        log_debug("telnet: error on %d[%s]", sock->fd,
                pent->addr);
        destroy_socket(sock);
        proxyscan_scan_next(pent);
        return NULL;
    }

    /* we should never get here... */
    log_warn("telnet: got to end of telnet hook!");
    return NULL;
}

/******************************************************************************
 * http stuff here
 ******************************************************************************/
void proxyscan_http(struct pscan_entry *pent) {
    char type[9] = "http";
    char *port = type + 4; /* port to check on */

    /* this is somewhat complicated, we need to determine what port to scan.
     * check flags in reverse order .. */
    if (pent->flags & PSCAN_FL_HTTP8000_CHECK) {
        strcpy(port, "8080");
        pent->flags |= PSCAN_FL_HTTP8080_CHECK;
    } else if (pent->flags & PSCAN_FL_HTTP3128_CHECK) {
        strcpy(port, "8000");
        pent->flags |= PSCAN_FL_HTTP8000_CHECK;
    } else if (pent->flags & PSCAN_FL_HTTP81_CHECK) {
        strcpy(port, "3128");
        pent->flags |= PSCAN_FL_HTTP3128_CHECK;
    } else if (pent->flags & PSCAN_FL_HTTP80_CHECK) {
        strcpy(port, "81");
        pent->flags |= PSCAN_FL_HTTP81_CHECK;
    } else {
        strcpy(port, "80");
        pent->flags |= PSCAN_FL_HTTP80_CHECK;
    }

    if (proxyscan_socket_connect(type, pent, port) == NULL)
        return; /* bogus. */

    pent->sock->udata = pent; /* point back to our pent */
    socket_monitor(pent->sock, SOCKET_FL_READ|SOCKET_FL_WRITE);

    add_hook(pent->sock->datahook, proxyscan_http_hook);
    pent->last = me.now;
}

/* a handy little macro to get the port number for the current check. */
#define http_which_port(pent)                                                 \
    (pent->flags & PSCAN_FL_HTTP8080_CHECK ? 8080 :                           \
     (pent->flags & PSCAN_FL_HTTP8000_CHECK ? 8000 :                          \
      (pent->flags & PSCAN_FL_HTTP3128_CHECK ? 3128 :                         \
       (pent->flags & PSCAN_FL_HTTP81_CHECK ? 81 : 80))))

HOOK_FUNCTION(proxyscan_http_hook) {
    isocket_t *sock = (isocket_t *)data;
    struct pscan_entry *pent = sock->udata;
    short pport;
    char request[128];
    int reqlen;
    char rcvbuf[128];
    int rcvlen;

    /* update our last hit time */
    pent->last = me.now;

    pport = http_which_port(pent);

    /* send a newline to prompt for a wingate reply, and then we can just wait
     * for data.  very handy. */
    if (SOCKET_WRITE(sock)) {
        reqlen = sprintf(request, "CONNECT %s:%s/ HTTP/1.1\nHost:%s:%s\n\n",
                proxyscan.target, proxyscan.target_port, proxyscan.target,
                proxyscan.target_port);
        if (!(rcvlen = socket_write(sock, request, reqlen))) {
            /* hrm, this is bad.  if we couldn't write assume it's not an open
             * proxy (hm!) and go down to the end of the function, where the
             * next request will be performed. */
            log_debug("http: couldn't write request to %d[%s:%d]", sock->fd,
                    pent->addr, pport);
            destroy_socket(sock);
            proxyscan_scan_next(pent);
            return NULL;
        } else if (reqlen != rcvlen) {
            log_warn("http: couldn't send full request to %d[%s:%d]", sock->fd,
                    pent->addr, pport);
            destroy_socket(sock);
            proxyscan_scan_next(pent);
            return NULL;
        }
        log_debug("http: sent request (%d %s) to %d[%s:%d]", reqlen, request,
                sock->fd, pent->addr, pport);
        return NULL;
    }
    if (SOCKET_READ(sock)) {
        rcvlen = socket_read(sock, rcvbuf, 127);
        if (rcvlen == 0)
            return NULL; /* keep waiting */
        else if (rcvlen < 0) {
            log_debug("http: socket error %s on %d[%s:%d]",
                    socket_strerror(sock), sock->fd, pent->addr, pport);
            destroy_socket(sock); /* it's definitely not a socks4 server */
            proxyscan_scan_next(pent);
            return NULL;
        }
        if (rcvlen != 128)
            rcvbuf[rcvlen] = '\0';
        else
            rcvbuf[127] = '\0';

        log_debug("http: got response from %d[%s:%d]: %d %s", sock->fd,
                pent->addr, pport, rcvlen, rcvbuf);

        /* see if we got a positive response.  this is easy to check, just
         * match against 'HTTP/1.? 2??*'.  Any positive response will be a
         * 200...  Also, according to lucas@DALnet, apparently some http
         * proxies don't even reply!  they just start sending data!  if we get
         * data that isn't an 'HTTP/' reply, consider it open too! */
        if ((rcvlen >= 12 && match("HTTP/1.? 2??*", rcvbuf)) ||
                match(proxyscan.target_pattern, rcvbuf)) {
            /* positive http response!  dump them on their asses. */
            destroy_socket(sock);

            /* another winner! */
            switch (pport) {
                case 80:
                    pent->flags |= PSCAN_FL_HTTP80_FOUND;
                    break;
                case 81:
                    pent->flags |= PSCAN_FL_HTTP81_FOUND;
                    break;
                case 3128:
                    pent->flags |= PSCAN_FL_HTTP3128_FOUND;
                    break;
                case 8000:
                    pent->flags |= PSCAN_FL_HTTP8000_FOUND;
                    break;
                case 8080:
                    pent->flags |= PSCAN_FL_HTTP8080_FOUND;
                    break;
            }
            proxyscan_scan_next(pent);
            return NULL;
        } else {
            /* XXX: we might want to buffer? */
            destroy_socket(sock);
            proxyscan_scan_next(pent);
            return NULL;
        }
    }
    if (SOCKET_ERROR(sock)) {
        log_debug("http: error on %d[%s:%d]", sock->fd,
                pent->addr, pport);
        destroy_socket(sock);
        proxyscan_scan_next(pent);
        return NULL;
    }

    /* we should never get here... */
    log_warn("http: got to end of http hook!");
    return NULL;
}

/******************************************************************************
 * miscellaneous internal use functions
 ******************************************************************************/
HOOK_FUNCTION(proxyscan_timer_hook) {
    struct pscan_entry *pent = (struct pscan_entry *)data;

    /* do timeouts. */
    pent->timer = TIMER_INVALID;
    if (pent->flags & PSCAN_FL_CACHE)
        proxyscan_destroy(pent);
    else {
        destroy_socket(pent->sock);
        proxyscan_scan_next(pent);
    }

    return NULL;
}

MODULE_LOADER(proxyscan) {
    conf_list_t *conf = *confdata;
    char *addr, *s;

    char *ctmp, *cold;
    conf_list_t *clp;

    /* initialize our structure.. */
    memset(&proxyscan, 0, sizeof(proxyscan));
    proxyscan.maxcount = 32768;
    proxyscan.checks = PSCAN_FL_ALL_CHECK;
    proxyscan.timeout = 20; /* default to timeout in 20 seconds.. */
    proxyscan.cache_expire = 3600; /* default to expire entries in one hour */

    /* now parse our settings */
    addr = "0.0.0.0";
    s = conf_find_entry("bind", conf, 1);
    if (s != NULL)
        addr = s;
    strncpy(proxyscan.bind, addr, IPADDR_MAXLEN);
    proxyscan.timeout = str_conv_time(conf_find_entry("timeout", conf, 1), 20);
    proxyscan.cache_expire =
     str_conv_time(conf_find_entry("expire", conf, 1), 3600);

    proxyscan.maxcount =
        str_conv_int(conf_find_entry("cache", conf, 1), 32768);

    clp = conf_find_list("target", conf, 1);
    if (clp == NULL) {
        log_error("proxyscan: must define a target to connect to!");
        return 0;
    } else {
        s = conf_find_entry("address", clp, 1);
        if (s != NULL)
            strncpy(proxyscan.target, s, IPADDR_MAXLEN);
        else {
            log_error("proxyscan: must define a target to connect to!");
            return 0;
        }
        proxyscan.target_port = conf_find_entry("port", clp, 1);
        if (proxyscan.target_port == NULL)
            proxyscan.target_port = "6667";
        s = conf_find_entry("pattern", clp, 1);
        if (s != NULL)
            proxyscan.target_pattern = strdup(s);
        else
            proxyscan.target_pattern = strdup("*");
    }

    clp = conf_find_list("skip", conf, 1);
    if (clp != NULL) {
        cold = ctmp = NULL;
        proxyscan.skipaddrs = NULL;
        proxyscan.skiptabsize = 0;

        while ((cold = ctmp = conf_find_entry_next("", cold, clp, 1)) !=
                NULL) {
            /* add this to the list */
            proxyscan.skiptabsize++;
            proxyscan.skipaddrs = realloc(proxyscan.skipaddrs,
                    sizeof(char *) * proxyscan.skiptabsize);
            proxyscan.skipaddrs[proxyscan.skiptabsize - 1] = strdup(ctmp);
        }
    }

    clp = conf_find_list("check", conf, 1);
    if (clp != NULL) {
        proxyscan.checks = 0; /* initialize our checks, they'll tell us what to
                                 do now. */
        if (str_conv_bool(conf_find_entry("socks4", clp, 1), 0))
            proxyscan.checks |= PSCAN_FL_SOCKS4_CHECK;
        if (str_conv_bool(conf_find_entry("socks5", clp, 1), 0))
            proxyscan.checks |= PSCAN_FL_SOCKS5_CHECK;
        if (str_conv_bool(conf_find_entry("telnet", clp, 1), 0))
            proxyscan.checks |= PSCAN_FL_TELNET_CHECK;
        if (str_conv_bool(conf_find_entry("http", clp, 1), 0))
            proxyscan.checks |= PSCAN_FL_HTTP_CHECK;
    }
        
    /* now setup events/hooks/etc */
    proxy_found = create_event(EVENT_FL_NORETURN);
    if (proxy_found == NULL)
        return 0;
    proxy_clean = create_event(EVENT_FL_NORETURN);
    if (proxy_clean == NULL)
        return 0;

    /* and other stuff */
    proxyscan.hash = create_hash_table(25147,
            offsetof(struct pscan_entry, addr), IPADDR_MAXLEN + 1,
            HASH_FL_STRING, "strncasecmp");
    if (proxyscan.hash == NULL)
        return 0;
    TAILQ_INIT(&proxyscan.queue);

    return 1; /* successfully loaded. */
}

MODULE_UNLOADER(proxyscan) {
    int i;
    
    /* free the memory from the skip table list */
    for (i = 0;i < proxyscan.skiptabsize;i++)
        free(proxyscan.skipaddrs[i]);
    free(proxyscan.skipaddrs);
    free(proxyscan.target_pattern);

    destroy_event(proxy_found);
    destroy_hash_table(proxyscan.hash);
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
