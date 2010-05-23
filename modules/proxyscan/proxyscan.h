/*
 * proxyscan.h: proxyscan module header for structures/etc
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: proxyscan.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef PROXYSCAN_PROXYSCAN_H
#define PROXYSCAN_PROXYSCAN_H

/* event for found proxies */
extern event_t *proxy_found;

/* event for clean proxies (this only needs to be trapped if you're really that
 * interested). */
extern event_t *proxy_clean;

/* list stuff for pscan_entry */
TAILQ_HEAD(pscan_entry_list, pscan_entry);
extern struct pscan_entry_list proxyscans;

/* The below structure contains all that is needed by the scanner to check and
 * cache hosts for various proxies.  We hold a isock_address for the address,
 * and a set of flags and other items for cacheing/checking purposes. */
struct pscan_entry {
    char    addr[IPADDR_MAXLEN + 1]; /* socket address */
    struct isocket *sock; /* the socket currently used for scanning purposes */
    time_t  hit;        /* this is the last time this entry was 'hit', used for
                           cache/expiry purposes. */
    time_t  last;        /* this is the last time this entry received some kind
                           of data from the currently open socket, useful for
                           timing out dead connections. */
    timer_ref_t timer;        /* the timer for this entry */

    void    *udata;        /* something to hold context/user data */

    int            flags;        /* entry flags (below) */
/* these four define what we're checking for, below that we define flags to
 * mark what we've found. */
#define PSCAN_FL_SOCKS4_CHECK        0x0001
#define PSCAN_FL_SOCKS5_CHECK        0x0002
#define PSCAN_FL_TELNET_CHECK        0x0004
#define PSCAN_FL_HTTP80_CHECK        0x0008
#define PSCAN_FL_HTTP81_CHECK        0x0010
#define PSCAN_FL_HTTP3128_CHECK        0x0020
#define PSCAN_FL_HTTP8000_CHECK        0x0040
#define PSCAN_FL_HTTP8080_CHECK        0x0080
#define PSCAN_FL_HTTP_CHECK        0x00F8
#define PSCAN_FL_ALL_CHECK        0x00FF

/* these are what we've found.  note that we classify HTTP proxies into five
 * flags, so 'PSCAN_FL_HTTP_FOUND' is actually a mask which covers all five of
 * them.  PSCAN_FL_OPEN will return true if the proxies are open at all. */
#define PSCAN_FL_SOCKS4_FOUND        0x0100
#define PSCAN_FL_SOCKS5_FOUND        0x0200
#define PSCAN_FL_TELNET_FOUND        0x0400
#define PSCAN_FL_HTTP80_FOUND        0x0800
#define PSCAN_FL_HTTP81_FOUND        0x1000
#define PSCAN_FL_HTTP3128_FOUND        0x2000
#define PSCAN_FL_HTTP8000_FOUND        0x4000
#define PSCAN_FL_HTTP8080_FOUND        0x8000
#define PSCAN_FL_HTTP_FOUND        0xF800
#define PSCAN_FL_OPEN                0xFF00
/* last but not least, a few other flags which tell us whether or not to cache
 * the result, and whether or not to check for everything even if one hole is
 * found. Additionally, a flag to hook the proxy_clean event is provided, if
 * you're interested. */
#define PSCAN_FL_NOCACHE        0x0001 << 16
#define PSCAN_FL_CHECKALL        0x0002 << 16
#define PSCAN_FL_NOTIFY_CLEAN        0x0004 << 16
#define PSCAN_FL_CACHE                0x0008 << 16

    TAILQ_ENTRY(pscan_entry) lp;
};

/* this function performs a proxy scan on the given socket.  It actually simply
 * lifts the address as needed from the socket to construct its own, and uses
 * that for scanning purposes.  The caller is responsible for hooking on the
 * 'proxy_found' event declared above to find out about open proxies.  Flags
 * can be any of the flags above which are masked against 0xFFFF0000 (meaning
 * the actual check/found flags cannot be passed, for obvious reasons).
 * Lastly, udata can be any data the caller wishes to associate with the scan.
 * This is a one-off, meaning that this data will be disassociated after any
 * relevant hooks are called for this scan.  The function returns the structure
 * created/found for the scan, or NULL if no structure could be created (for
 * instance if there's a check in progress). */
struct pscan_entry *proxy_scan(char *, int, void *);
#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
