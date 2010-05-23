/*
 * dns.c: some support code for the dns module
 * 
 * Copyright 2003 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * Most of the code in this file is used to handle configuring the server at
 * startup and cleaning up at shutdown.  Most of the other useful code is
 * contained in other files.
 */

#include <ithildin/stand.h>

#include "dns.h"
#include "lookup.h"

IDSTRING(rcsid, "$Id: dns.c 848 2010-04-30 01:57:25Z wd $");

MODULE_REGISTER("1.1");

struct dns_data_struct dns;

HOOK_FUNCTION(dns_socket_hook);
HOOK_FUNCTION(dns_reload_hook);
int dns_parse_conf(conf_list_t *conf);

HOOK_FUNCTION(dns_socket_hook) {
    static unsigned char pkt[DNS_MAX_PACKET_SIZE];
    int size = 0;
    isocket_t *sock = (isocket_t *)data;
        
    /* first send off any queries we might have */
    while (dns_lookup_send())
        ; /* no-op.  just loop on the function until it returns 0 (meaning
             there's nothing left or the socket became un-writeable) */

    if (SOCKET_READ(sock)) {
        do {
            size = socket_read(dns.sock, pkt, DNS_MAX_PACKET_SIZE);
            if (size < 0) {
                /* XXX this is probably a big problem and should be handled 
                 * better */
                log_error("socket_read(dns_sock): %s",
                        socket_strerror(dns.sock));
                return NULL;
            }
            if (size)
                dns_packet_parse(pkt, size);
        } while (size > 0);
    }

    return NULL;
}

/* examine a lookup to see if it should be expired, or retried */
HOOK_FUNCTION(dns_timer_hook) {
    dns_lookup_t *dlp = (dns_lookup_t *)data;

    dlp->timer = TIMER_INVALID;
    if (dlp->flags & DNS_LOOKUP_FL_CACHE) {
        destroy_dns_lookup(dlp);
        return NULL;
    }

    assert(dlp->retry >= 0 && dlp->retry <= dns.pending.retries);

    /* if it's not a cached entry (darn) we need to figure out if it needs a
     * retry or not.. */
    if (dlp->retry == 0) {
        /* if 'ttl' is 0 (meaning we've hit the most retries possible)
         * we mark the lookup as a failed timeout and call the finish
         * function */
        dlp->flags |= DNS_LOOKUP_FL_TIMEOUT | DNS_LOOKUP_FL_FAILED;
        dns_lookup_finish(dlp);
    } else {
        /* otherwise it means we have to retry.  decrement the count, and
         * move the lookup back to the waiting list. */
        dlp->retry--;
        dns_lookup_move(dlp, DNS_LOOKUP_FL_WAITING, true);
    }

    /* try this for the heck of it.. */
    while (dns_lookup_send());

    return NULL;
}

HOOK_FUNCTION(dns_reload_hook) {

    /* if the parser fails, there *may* be trouble. :/ */
    if (!dns_parse_conf(*dns.confdata))
        log_warn("a problem occured while parsing the configuration data for "
                "the dns module.  lookups may no longer work.");

    return NULL;
}

int dns_parse_conf(conf_list_t *conf) {
    char *s, *nameserver;
    char *addr = "0.0.0.0"; /* IPv4 INADDR_ANY by default. */
    char *qport = NULL;
    int i;

    qport = conf_find_entry("queryport", conf, 1);
    if (qport == NULL)
        qport = int_conv_str(DNS_DEFAULT_PORT);
    s = conf_find_entry("bind", conf, 1);
    if (s != NULL)
        addr = s;
    s = conf_find_entry("nameserver", conf, 1);
    if (s == NULL)
        s = "auto";

    if (!strcasecmp(s, "auto")) {
        FILE *fp = fopen("/etc/resolv.conf", "r");
        char buf[1024], *s2;
        if (fp != NULL) {
            while ((fgets(buf, 1024, fp)) != NULL) {
                if (!strncasecmp(buf, "nameserver", 10)) {
                    s = buf + 10;
                    while (isspace(*s))
                        s++;
                    s2 = s;
                    while (!isspace(*s2))
                        s2++;
                    *s2 = '\0';

                    break;
                }
            }
            fclose(fp);
        }
    }
    nameserver = s;

    dns.pending.timeout = str_conv_time(
            conf_find_entry("lookup-timeout", conf, 1), 10);
    dns.pending.retries = str_conv_int(
            conf_find_entry("lookup-retries", conf, 1), 2);
    dns.pending.max = str_conv_int(
            conf_find_entry("lookup-concurrent-max", conf, 1), 128);
    if (dns.pending.max < 1 || dns.pending.max > 32767) {
        log_warn("dns lookup-concurrent-max %d invalid.  setting to %d",
                dns.pending.max, 32767);
        dns.pending.max = 32767;
    }
    dns.cache.expire = str_conv_time(
            conf_find_entry("cache-expire", conf, 1), 3600);
    dns.cache.failure = str_conv_bool(
            conf_find_entry("cache-failures", conf, 1), 1);
    dns.cache.max = str_conv_int(
            conf_find_entry("cache-size", conf, 1), 256);
    if (dns.cache.max < 0) {
        log_warn("dns cache-size %d invalid.  setting to 256",
                dns.cache.max);
        dns.cache.max = 256;
    }

    /* fill in the 'retry time' table correctly.  it is reverse mapped such
     * that (lookup)->ttl provides the correct timeout value.  e.g. for timeout
     * 5 and 2 retries the table would be 20, 10, 5. */
    if (dns.pending.retry_times != NULL)
        free(dns.pending.retry_times);
    dns.pending.retry_times =
        calloc(1, sizeof(time_t) * (dns.pending.retries + 1));
    dns.pending.retry_times[dns.pending.retries] = dns.pending.timeout;
    for (i = dns.pending.retries - 1;i >= 0;i--)
        dns.pending.retry_times[i] = dns.pending.retry_times[i + 1] * 2;

    /* set up our socket.  if we already had a socket, bomb it and create a new
     * one. */
    if (dns.sock != NULL)
        destroy_socket(dns.sock);
    if ((dns.sock = create_socket()) == NULL) {
        log_error("couldn't create dns socket, dns won't work");
        return 0;
    }
    if (!set_socket_address(isock_laddr(dns.sock), addr, NULL,
                SOCK_DGRAM)) {
        log_error("couldn't set socket address of dns socket to %s", addr);
        return 0;
    }
    if (!open_socket(dns.sock)) {
        log_error("couldn't open dns socket, dns won't work.");
        return 0;
    }

    /* setup our socket.  connect to our chosen nameserver (as such), add our
     * data hook, and set our monitoring for I/O */
    if (!socket_connect(dns.sock, nameserver, qport, SOCK_DGRAM)) {
        log_error("couldn't bind dns socket packets to %s/%s", nameserver,
                qport);
        return 0;
    }
        
    /* now add a hook for our socket and keep an eye on it */
    socket_monitor(dns.sock, SOCKET_FL_READ | SOCKET_FL_WRITE);
    add_hook(dns.sock->datahook, dns_socket_hook);

    log_notice("dns module using nameserver %s/%s", nameserver, qport);

    /* flush the cache too */
    while (!TAILQ_EMPTY(&dns.cache.list))
        destroy_dns_lookup(TAILQ_FIRST(&dns.cache.list));

    return 1;
}

/* here are the string-form versions of the class and type lists.  they are
 * used by the two conversion macros and two conversion functions to change
 * numbers to strings and vice versa. */
const char *const dns_class_strlist[256] = { NULL,
    "IN", NULL, "CH", "HS", /* following this are 250 NULLs.  fuh */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "NONE",
    "ANY"
};
dns_class_t dns_str_conv_class(const char *str) {
    int i;

    for (i = 1;i <= DNS_C_ANY;i++) {
        if (dns_class_strlist[i] != NULL &&
                !strcasecmp(str, dns_class_strlist[i]))
            return i;
    }

    return 0;
}

/* Here's the type listing, more interesting than the class one. */
const char *const dns_type_strlist[256] = { NULL,
    "A", "NS", "MD", "MF", "CNAME", "SOA", "MB", "MG", "MR", "NULL",
    "WKS", "PTR", "HINFO", "MINFO", "MX", "TXT", "RP", "AFSDB", "X25", "ISDN",
    "RT", "NSAP", NULL, "SIG", "KEY", "PX", NULL, "AAAA", "LOC", "NXT",
    "EID", "NIMLOC", "SRV", "ATMA", "NAPTR", NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, "ANY"
};
dns_type_t dns_str_conv_type(const char *str) {
    int i;

    for (i = 1;i <= DNS_T_ANY;i++) {
        if (dns_type_strlist[i] != NULL &&
                !strcasecmp(str, dns_type_strlist[i]))
            return i;
    }

    return 0;
}
    
MODULE_LOADER(dns) {

    dns.confdata = confdata;
    if (!dns_parse_conf(*confdata))
        return 0; /* conf parser failure! */

    add_hook(me.events.read_conf, dns_reload_hook);
    return 1;
}

MODULE_UNLOADER(dns) {
    
    while (!TAILQ_EMPTY(&dns.pending.alist))
        destroy_dns_lookup(TAILQ_FIRST(&dns.pending.alist));
    while (!TAILQ_EMPTY(&dns.pending.wlist))
        destroy_dns_lookup(TAILQ_FIRST(&dns.pending.wlist));
    while (!TAILQ_EMPTY(&dns.cache.list))
        destroy_dns_lookup(TAILQ_FIRST(&dns.cache.list));

    destroy_socket(dns.sock);
    remove_hook(me.events.read_conf, dns_reload_hook);
}


/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
