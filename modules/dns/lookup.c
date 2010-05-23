/*
 * lookup.c: dns lookup handling code
 * 
 * Copyright 2002, 2003 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains the necessary bits to handle creating/destroying/working
 * with lookups.
 */

#include <ithildin/stand.h>

#include "dns.h"
#include "lookup.h"

IDSTRING(rcsid, "$Id: lookup.c 611 2005-11-22 10:32:23Z wd $");

static dns_lookup_t *find_dns_lookup(dns_class_t, dns_type_t, unsigned char *);

/* this function constructs a dns_lookup item and places it on the queue of
 * pending lookups.  if the pending queue is empty it tries to send the lookup
 * immediately.  In general the function assumes that 'data' is actually a
 * character string representing the host entry that needs to be looked up.
 * Conversion may be performed by the packet functions as necessary (especially
 * for the 'A' and 'AAAA' types, where it is assumed that the address is in
 * standard form and needs to be converted to use in-addr.arpa or ip6.arpa,
 * respectively) */
dns_lookup_t *dns_lookup(dns_class_t class, dns_type_t type,
        unsigned char *data, hook_function_t callback) {
    dns_lookup_t *dlp;

    /* if dlen is very large we need to complain.  Hoepfully this won't ever
     * happen. */
    if (strlen(data) > DNS_MAX_NAMELEN) {
        log_warn("dns_lookup(%s, %s, %p): dlen > %d",
                dns_class_conv_str(class), dns_type_conv_str(type), data,
                DNS_MAX_NAMELEN);
        return NULL;
    }

    /* Try finding the associated lookup first.  If it's pending, just add the
     * hook onto the event and return.  It if *isn't* pending, return our
     * answer immediately.  If we don't find it, create a new lookup and fill
     * it in and try to send it off at once. */
    if ((dlp = find_dns_lookup(class, type, data)) != NULL) {
        if (dlp->flags & DNS_LOOKUP_FL_CACHE) {
            /* it's in the cache.  do a couple of things here:
             * move it up to the head of the cache (this keeps the most recent
             * entries at the head so that entries can be popped off the tail
             * when new entries must be cached) and return the result
             * immediately to the caller. */
            TAILQ_REMOVE(&dns.cache.list, dlp, lp);
            TAILQ_INSERT_HEAD(&dns.cache.list, dlp, lp);

            callback(NULL, (void *)dlp);
            return dlp;
        } else {
            /* it's not on the cache, just add this hook in and return */
            add_hook(dlp->finished, callback);
            return dlp;
        }
    }

    /* it's a new entry.  fill it in and send the query.. */
    dlp = calloc(1, sizeof(dns_lookup_t));

    dlp->finished = create_event(EVENT_FL_NORETURN);
    add_hook(dlp->finished, callback);
    dlp->class = class;
    dlp->type = type;
    strlcpy(dlp->data, data, DNS_MAX_NAMELEN + 1);

    dlp->id = dns.pending.idn++;
    dlp->last = me.now;
    dlp->retry = dns.pending.retries;
    dlp->timer = TIMER_INVALID;

    /* now add it to the waiting lookup list.  if our dns socket is writeable,
     * try doing a send here too.  If the socket is writeable and acount is not
     * equal to the maximum we should be the only entry on the waiting list. */
    dns_lookup_move(dlp, DNS_LOOKUP_FL_WAITING, false);
    dlp->flags |= DNS_LOOKUP_FL_WAITING;
    dns_lookup_send();

    return dlp;
}

/* this function walks the list of waiting/active pending lookups and removes
 * the callback hook from the event.  If the event has no hooks and the lookup
 * is waiting, we destroy the lookup */
void dns_lookup_cancel(hook_function_t callback) {
    dns_lookup_t *dlp, *dlp2;

    TAILQ_FOREACH(dlp, &dns.pending.alist, lp)
        remove_hook(dlp->finished, callback);

    dlp = TAILQ_FIRST(&dns.pending.wlist);
    while (dlp != NULL) {
        dlp2 = TAILQ_NEXT(dlp, lp);
        remove_hook(dlp->finished, callback);
        if (EVENT_HOOK_COUNT(dlp->finished) == 0)
            destroy_dns_lookup(dlp);
        dlp = dlp2;
    }
}

/* this moves a lookup to the appropriate list, and sets the right flags and
 * unsets the wrong flags on it.  this saves a lot of duplicated work which is
 * several lines long in various places and prone to error.  now all the errors
 * are here. :) */
void dns_lookup_move(dns_lookup_t *dlp, int list, bool head) {
    struct dns_lookup_tailq *dltp;
    
    if (dlp->flags & DNS_LOOKUP_FL_WAITING)
        TAILQ_REMOVE(&dns.pending.wlist, dlp, lp);
    else if (dlp->flags & DNS_LOOKUP_FL_PENDING) {
        TAILQ_REMOVE(&dns.pending.alist, dlp, lp);
        dns.pending.acount--;
    } else if (dlp->flags & DNS_LOOKUP_FL_CACHE) {
        TAILQ_REMOVE(&dns.cache.list, dlp, lp);
        dns.cache.count--;
    }

    dlp->flags &= ~(DNS_LOOKUP_FL_WAITING | DNS_LOOKUP_FL_PENDING |
            DNS_LOOKUP_FL_CACHE);
    dlp->flags |= list;

    if (list == DNS_LOOKUP_FL_WAITING)
        dltp = &dns.pending.wlist;
    else if (list == DNS_LOOKUP_FL_PENDING) {
        dltp = &dns.pending.alist;
        dns.pending.acount++;
    } else if (list == DNS_LOOKUP_FL_CACHE) {
        dltp = &dns.cache.list;
        dns.cache.count++;
    } else
        return;

    if (head || TAILQ_EMPTY(dltp))
        TAILQ_INSERT_HEAD(dltp, dlp, lp);
    else
        TAILQ_INSERT_TAIL(dltp, dlp, lp);
}

/* This completely obliterates a lookup, returns all memory from it and any RRs
 * it contains. */
void destroy_dns_lookup(dns_lookup_t *dlp) {
    struct dns_rr *drp;
    
    /* remove it from whatever list it's on .. */
    dns_lookup_move(dlp, 0, false);
    destroy_event(dlp->finished);

    /* Clear out all the RRs it might have .. */
    while ((drp = LIST_FIRST(&dlp->rrs.an)) != NULL) {
        LIST_REMOVE(drp, lp);
        if (drp->rdlen != 0)
            free(drp->rdata.txt);
        free(drp);
    }
    while ((drp = LIST_FIRST(&dlp->rrs.ns)) != NULL) {
        LIST_REMOVE(drp, lp);
        if (drp->rdlen != 0)
            free(drp->rdata.txt);
        free(drp);
    }
    while ((drp = LIST_FIRST(&dlp->rrs.ad)) != NULL) {
        LIST_REMOVE(drp, lp);
        if (drp->rdlen != 0)
            free(drp->rdata.txt);
        free(drp);
    }

    if (dlp->timer != TIMER_INVALID)
        destroy_timer(dlp->timer);

    /* and destroy the lookup.  yay. */
    free(dlp);
}

/* This function takes an RR and turns it into a "pretty" string of the form:
 * HOST <TTL> <CLASS> <TYPE> [DATA...] 
 * If the buffer for the data isn't large enough an ellipsis is appended
 * to the string. */
const char *dns_lookup_pretty_rr(struct dns_rr *drp) {
#define PRETTY_BUFSIZE 512
    static char str[PRETTY_BUFSIZE];
    int idx;

    idx = snprintf(str, PRETTY_BUFSIZE, "%s %s %s %s", drp->name,
            time_conv_str(drp->ttl), dns_class_conv_str(drp->class),
            dns_type_conv_str(drp->type));
    if (drp->rdlen == 0)
        return str;

    switch (drp->type) {
        case DNS_T_HINFO:
            snprintf(str + idx, PRETTY_BUFSIZE - idx, " \"%s\" \"%s\"",
                    drp->rdata.hinfo->cpu, drp->rdata.hinfo->os);
            break;
        case DNS_T_MINFO:
            snprintf(str + idx, PRETTY_BUFSIZE - idx, " %s %s",
                    drp->rdata.minfo->rmailbx, drp->rdata.minfo->emailbx);
            break;
        case DNS_T_MX:
            snprintf(str + idx, PRETTY_BUFSIZE - idx, " %hu %s",
                    drp->rdata.mx->preference, drp->rdata.mx->exchange);
            break;
        case DNS_T_SOA:
            idx += snprintf(str + idx, PRETTY_BUFSIZE - idx, " %s %s (%u %s ",
                    drp->rdata.soa->mname, drp->rdata.soa->rname,
                    drp->rdata.soa->serial,
                    time_conv_str(drp->rdata.soa->refresh));
            idx += snprintf(str + idx, PRETTY_BUFSIZE - idx, "%s ",
                    time_conv_str(drp->rdata.soa->retry));
            idx += snprintf(str + idx, PRETTY_BUFSIZE - idx, "%s ",
                    time_conv_str(drp->rdata.soa->expire));
            idx += snprintf(str + idx, PRETTY_BUFSIZE - idx, "%s)",
                    time_conv_str(drp->rdata.soa->minimum));
            break;
        default:
            snprintf(str + idx, PRETTY_BUFSIZE - idx, " %s", drp->rdata.txt);
            break;
    }

    return str;
}

/* search for a dns lookup.  try the cache, then the active, then the waiting
 * pending lists. */
static dns_lookup_t *find_dns_lookup(dns_class_t class, dns_type_t type,
        unsigned char *data) {
    dns_lookup_t *dlp;

#define FIND_LOOKUP(_list) do {                                                 \
    TAILQ_FOREACH(dlp, _list, lp) {                                             \
        if (dlp->class == class && dlp->type == type &&                         \
                !strcasecmp(data, dlp->data))                                   \
            return dlp;                                                         \
    }                                                                           \
} while (0)
    FIND_LOOKUP(&dns.cache.list);
    FIND_LOOKUP(&dns.pending.alist);
    FIND_LOOKUP(&dns.pending.wlist);

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
