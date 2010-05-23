/*
 * lookup.h: header file to describe dns lookups
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: lookup.h 611 2005-11-22 10:32:23Z wd $
 */

#ifndef DNS_LOOKUP_H
#define DNS_LOOKUP_H

#include "dns.h"

LIST_HEAD(dns_rr_list, dns_rr);

typedef struct dns_lookup {
    event_t *finished;                  /* event hooked when the lookup finishes */

    uint16_t class;                     /* class and type of lookup */
    uint16_t type;
    unsigned char data[DNS_MAX_NAMELEN + 1]; /* data being looked up */

    /* RRs returned in answer.  Some or all lists may be empty! */
    struct {
        struct dns_rr_list an;          /* answers */
        struct dns_rr_list ns;          /* authoritative servers */
        struct dns_rr_list ad;          /* additional data */
    } rrs;

#define DNS_LOOKUP_FL_WAITING   0x0001
#define DNS_LOOKUP_FL_PENDING   0x0002
#define DNS_LOOKUP_FL_OK        0x0004
#define DNS_LOOKUP_FL_AGAIN     0x0008
#define DNS_LOOKUP_FL_FAILED    0x0010
#define DNS_LOOKUP_FL_TIMEOUT   0x0020
#define DNS_LOOKUP_FL_NXDOMAIN  0x0040
#define DNS_LOOKUP_FL_CACHE     0x8000
    uint16_t flags;                     /* lookup flags */
    uint16_t id;                        /* lookup id */
    time_t last;                        /* last time this lookup was active */
    time_t ttl;                         /* maximum time to keep this lookup (in the
                                           cache */

    unsigned int retry;                 /* retry count for the lookup */
    timer_ref_t timer;                  /* expiration timer */

    TAILQ_ENTRY(dns_lookup) lp;
} dns_lookup_t;

dns_lookup_t *dns_lookup(dns_class_t, dns_type_t, unsigned char *,
        hook_function_t);
void dns_lookup_cancel(hook_function_t);
void dns_lookup_move(dns_lookup_t *, int, bool);
void destroy_dns_lookup(dns_lookup_t *);

const char *dns_lookup_pretty_rr(struct dns_rr *);

/* this one is in packet.c */
void dns_lookup_finish(dns_lookup_t *);
#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
