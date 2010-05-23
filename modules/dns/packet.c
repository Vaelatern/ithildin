/*
 * packet.c: dns packet parsing code
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains the gory packet parsing functions.  Basically it revolves
 * around two functions, the function for sending out lookups and the function
 * for parsing lookup replies.
 */

#include <ithildin/stand.h>

#include "dns.h"
#include "lookup.h"

IDSTRING(rcsid, "$Id: packet.c 822 2008-11-07 03:28:26Z wd $");

static int extract_rrs(unsigned char *, size_t, int, int,
        struct dns_rr_list *);

/* this function attempts to send any waiting queries to the nameserver.  it
 * will return 1 if there was a query to be sent and it sent the query.  It
 * handles moving pending queries from the waiting list to the active list, et
 * cetera. */
int dns_lookup_send(void) {
    struct dns_packet_header hdr;
    struct dns_query qry;
    dns_lookup_t *dlp;
    char pkt[DNS_MAX_PACKET_SIZE + DNS_MAX_NAMELEN];
    int nlen, plen;

    if (dns.pending.acount == dns.pending.max ||
            (dlp = TAILQ_FIRST(&dns.pending.wlist)) == NULL)
        return 0; /* no work to do */

    /* first move it to the active list .. */
    dns_lookup_move(dlp, DNS_LOOKUP_FL_PENDING, false);

    /* set up our header */
    memset(&hdr, 0, sizeof(hdr));
    hdr.id = htons(dlp->id);
    hdr.rd = 1; /* we want recursion from the nameserver */
    hdr.qdcount = htons(1); /* always one question for us */

    /* now set up our query.  in most cases we don't do anything special with
     * the user data in dlp, but for the PTR type we try to rewrite the
     * (assumedly) presentation format address into one suitable to the
     * nameserver. */
    if (dlp->class == DNS_C_IN && dlp->type == DNS_T_PTR) {
        int c[4];
#ifdef INET6
        unsigned char *s, *s2;
        struct in6_addr i6a;
        int i;

        if ((s = strchr(dlp->data, ':')) != NULL && strchr(s, ':') != NULL) {
            if (inet_pton(PF_INET6, dlp->data, &i6a) != 1) {
                dlp->flags |= DNS_LOOKUP_FL_FAILED;
                dns_lookup_finish(dlp);
                return 1;
            }

            s = qry.name;
            s2 = (char *)&i6a;
            /* if this lookup has failed and the 'again' flag is set, assume
             * that our last lookup was for ip6.arpa.  this time to a lookup in
             * ip6.int */
            if (dlp->flags & DNS_LOOKUP_FL_FAILED && dlp->flags &
                    DNS_LOOKUP_FL_AGAIN) {
                dlp->flags &= ~(DNS_LOOKUP_FL_FAILED | DNS_LOOKUP_FL_AGAIN);
                for (i = 15;i >= 0;i--)
                    sprintf(s + ((15 - i) * 4), "%x.%x.", s2[i] & 0xf,
                            (s2[i] >> 4) & 0xf);
                strcat(s, "ip6.int");
            } else {
                /* try putting it together in bitstring format .. */
#if 0
                strcpy(s, "\\[x");
                s += 3;
                for (i = 0;i < 16;i++)
                    sprintf(s + (i * 2), "%x%x", (s2[i] >> 4) & 0xf,
                            s2[i] & 0xf);
                strcat(s, "/128].ip6.arpa");
#else
                /* Trying alternate code here... */
                for (i = 15;i >= 0;i--)
                    sprintf(s + ((15 - i) * 4), "%x.%x.",
                            s2[i] & 0xf, (s2[i] >> 4) & 0xf);
                strcat(s, "ip6.arpa");
#endif
            }
        } else
#endif
        {
            sscanf(dlp->data, "%d.%d.%d.%d", &c[3], &c[2], &c[1], &c[0]);
            sprintf(qry.name, "%d.%d.%d.%d.in-addr.arpa", c[0], c[1], c[2],
                    c[3]);
        }
    } else
        strlcpy(qry.name, dlp->data, DNS_MAX_NAMELEN + 1);
    
    qry.type = htons(dlp->type);
    qry.class = htons(dlp->class);

    /* now construct the packet and send it along.. */
    memcpy(pkt, &hdr, sizeof(hdr));
    plen = sizeof(hdr);
    plen += dn_comp(qry.name, pkt + plen, DNS_MAX_NAMELEN, NULL, NULL);
    memcpy(pkt + plen, &qry.type, sizeof(uint16_t));
    plen += sizeof(uint16_t);
    memcpy(pkt + plen, &qry.class, sizeof(uint16_t));
    plen += sizeof(uint16_t);
    
    if (plen > DNS_MAX_PACKET_SIZE) {
        /* oh boy .. */
        log_warn("constructed a dns query with size > %d for %s %s %s",
                DNS_MAX_PACKET_SIZE, dns_class_conv_str(dlp->class),
                dns_type_conv_str(dlp->type), qry.name);
        dlp->flags |= DNS_LOOKUP_FL_FAILED;
        dns_lookup_finish(dlp);
        return 1;
    }

    /* and send the packet */
    if ((nlen = socket_write(dns.sock, pkt, plen)) != plen) {
        /* this is silly.. let's try and dtrt here.  if the query didn't send
         * correctly then push it back on to the wait list and return 0. */
        dns_lookup_move(dlp, DNS_LOOKUP_FL_WAITING, true);
        log_debug("failed to send dns query for %s %s %s (tried %d, sent %d)",
                dns_class_conv_str(dlp->class), dns_type_conv_str(dlp->type),
                qry.name, plen, nlen);
        return 0;
    }

    if (dlp->timer != TIMER_INVALID) {
        log_warn("a timer exists for a pending lookup for %s %s %s!",
                dns_class_conv_str(dlp->class), dns_type_conv_str(dlp->type),
                qry.name);
        destroy_timer(dlp->timer);
    }

    assert(dlp->retry >= 0 && dlp->retry <= dns.pending.retries);
    dlp->timer = create_timer(0, dns.pending.retry_times[dlp->retry],
            dns_timer_hook, dlp);

    return 1; /* oookay */
}

/* parse a packet returned from the server.  we assume the packet is complete
 * (so underlying routines will have to make sure that is the case) and do
 * (hopefully) careful checks on it before returning the result. */
void dns_packet_parse(unsigned char *pkt, size_t plen) {
    struct dns_packet_header hdr;
    struct dns_query qry;
    int pidx = 0; /* index into the packet.  basically a count of how many
                     bytes we have sucked out of it so far. */
    dns_lookup_t *dlp;
    int rc;

    if (plen < sizeof(struct dns_packet_header) + 4) {
        log_warn("received malformed dns packet (size %d too small!)", plen);
        return;
    }
    /* bring in the header, and then reformat it for our host byte order.
     * find the query with the id (if any) and do some quick error checks */
    memcpy(&hdr, pkt, sizeof(hdr));
    pidx += sizeof(hdr);
    hdr.id = ntohs(hdr.id);
    hdr.qdcount = ntohs(hdr.qdcount);
    hdr.ancount = ntohs(hdr.ancount);
    hdr.nscount = ntohs(hdr.nscount);
    hdr.adcount = ntohs(hdr.adcount);

    /* search for our lookup */
    TAILQ_FOREACH(dlp, &dns.pending.alist, lp) {
        if (dlp->id == hdr.id)
            break;
    }
    /* try our waiting list too, sometimes replies will come late */
    if (dlp == NULL) {
        TAILQ_FOREACH(dlp, &dns.pending.wlist, lp) {
            if (dlp->id == hdr.id && dlp->retry < dns.pending.retries)
                break;
        }
    }
    /* if we didn't find it send a debug notice (since we don't want to warn
     * and allow a DoS from people flooding in fake dns packets) and return */
    if (dlp == NULL) {
        log_debug("Got dns reply with unknown id %d", hdr.id);
        return;
    }
    /* now do some sanity checks on the header */
    if (hdr.qdcount != 1 || !hdr.qr || hdr.opcode) {
        log_error("received a mangled dns packet (qdcount=%d qr=%d, opcode=%d)",
                hdr.qdcount, hdr.qr, hdr.opcode);
        return;
    }
    if (hdr.tc) {
        /* XXX: we should do TCP lookups in the case of truncated replies.
         * will do that in the future.. */
        log_debug("got truncated dns reply for %s", dlp->data);
        dlp->flags |= DNS_LOOKUP_FL_FAILED;
        dns_lookup_finish(dlp);
        return;
    }

    /* extract our question, too */
    if ((rc = dn_expand(pkt, pkt + plen, pkt + pidx, qry.name,
            DNS_MAX_NAMELEN + 1)) < 1) {
        log_error("dn_expand (%d) returned %d", __LINE__, rc);
        return;
    }
    pidx += rc;
    memcpy(&qry.type, pkt + pidx, sizeof(qry.type));
    qry.type = ntohs(qry.type);
    pidx += sizeof(qry.type);
    memcpy(&qry.class, pkt + pidx, sizeof(qry.class));
    qry.class = ntohs(qry.class);
    pidx += sizeof(qry.class);

    /* Examine our return code.  See if the server doesn't much like us. */
    switch (hdr.rcode) {
        case DNS_R_OK:
        case DNS_R_NXDOMAIN:
            break; /* skip */
        case DNS_R_SERVFAIL:
            /* in this case of failure the RFC is not very clear, it is
             * considered 'temporary' but conventional wisdom says that this
             * is not something that will resolve in a sane timeout cycle of
             * a lookup so I have opted to treat servfail as a 'permanent'
             * failure for this lookup. */
            dlp->flags |= DNS_LOOKUP_FL_FAILED;
            dns_lookup_finish(dlp);
            return;
        case DNS_R_BADFORMAT:
        case DNS_R_NOTIMP:
        case DNS_R_REFUSED:
        default:
            /* these are non-recoverable errors */
            log_error("nameserver returned error on query for %s %s %s "
                    "(%d %s)", dns_class_conv_str(dlp->class),
                    dns_type_conv_str(dlp->type), qry.name, hdr.rcode,
                    (hdr.rcode == DNS_R_BADFORMAT ? "bad format" :
                     (hdr.rcode == DNS_R_NOTIMP ? "not implemented" :
                      (hdr.rcode == DNS_R_REFUSED ? "query refused" :
                       "unknown"))));
            dlp->flags |= DNS_LOOKUP_FL_FAILED;
            dns_lookup_finish(dlp);
            return;
    }

    /* if we got NXDOMAIN from the nameserver and this is an IPv6 PTR request
     * in the ip6.arpa domain don't even bother with the answers, just re-send
     * this lookup and wait.. */
    if (hdr.rcode == DNS_R_NXDOMAIN && qry.class == DNS_C_IN &&
            qry.type == DNS_T_PTR) {
        char *s = qry.name + strlen(qry.name) - 8;
        if (!strcasecmp(s, "ip6.arpa")) {
            dlp->flags |= DNS_LOOKUP_FL_FAILED | DNS_LOOKUP_FL_AGAIN;
            dns_lookup_move(dlp, DNS_LOOKUP_FL_WAITING, true);
            return;
        }
    }

    /* mwokay.  now we just need to suck out the answer/authority/additional
     * sections.  Simple enough ... just use the function below.. */
    pidx = extract_rrs(pkt, plen, pidx, hdr.ancount, &dlp->rrs.an);
    pidx = extract_rrs(pkt, plen, pidx, hdr.nscount, &dlp->rrs.ns);
    extract_rrs(pkt, plen, pidx, hdr.adcount, &dlp->rrs.ad);

    /* If rcode was NXDOMAIN or we have no answer section then we set the
     * NXDOMAIN flag.  Apparently the reply can contain NSes in the authority
     * section to refer us to them, and we *should* (XXX) honor that, but for
     * now we don't. */
    if (hdr.rcode == DNS_R_NXDOMAIN || LIST_EMPTY(&dlp->rrs.an))
        dlp->flags |= DNS_LOOKUP_FL_NXDOMAIN | DNS_LOOKUP_FL_FAILED;
    /* Otherwise we should look to see if we asked for something that wasn't a
     * CNAME and only got CNAMEs back, and then follow up on those CNAMEs.  We
     * don't do that -- yet. (XXX) */

    dns_lookup_finish(dlp);
}

/* this function extracts count RRs from pkt starting at index 'pidx' and
 * places them in list.  It returns the new value of pidx when done.
 * Additionally, for certain types, we decompress the rdata here (specifically
 * A, AAAA, HINFO, MINFO, WKS, SOA, NS, CNAME, and PTR) */
static int extract_rrs(unsigned char *pkt, size_t plen, int pidx, int count,
    struct dns_rr_list *list) {
    struct dns_rr *drp, *last_rr = NULL;
    int rc;
    unsigned char *rdata;

    last_rr = NULL;
    while (count) {
        drp = calloc(1, sizeof(struct dns_rr));

        if ((rc = dn_expand(pkt, pkt + plen, pkt + pidx, drp->name,
                        DNS_MAX_NAMELEN + 1)) < 1) {
            log_error("dn_expand (%d) returned %d", __LINE__, rc);
            return pidx;
        }
        pidx += rc;
        memcpy(&drp->type, pkt + pidx, sizeof(uint16_t));
        drp->type = ntohs(drp->type);
        pidx += sizeof(uint16_t);
        memcpy(&drp->class, pkt + pidx, sizeof(uint16_t));
        drp->class = ntohs(drp->class);
        pidx += sizeof(uint16_t);
        memcpy(&drp->ttl, pkt + pidx, sizeof(uint32_t));
        drp->ttl = ntohl(drp->ttl);
        pidx += sizeof(uint32_t);
        memcpy(&drp->rdlen, pkt + pidx, sizeof(uint16_t));
        drp->rdlen = ntohs(drp->rdlen);
        pidx += sizeof(uint16_t);
        if (drp->rdlen > 0) {
            int sidx = 0;

            rdata = pkt + pidx;
            pidx += drp->rdlen;

            /* Fill in the data.  We do a copy over first, and if we need to
             * realloc and make changes below we do that too */
            drp->rdata.txt = calloc(1, drp->rdlen);
            memcpy(drp->rdata.txt, rdata, drp->rdlen);
            switch (drp->type) {
                case DNS_T_A:
                    if (drp->class == DNS_C_IN) {
                        drp->rdlen = IPADDR_MAXLEN + 1;
                        drp->rdata.txt = realloc(drp->rdata.txt, drp->rdlen);
                        inet_ntop(PF_INET, rdata, drp->rdata.txt, drp->rdlen);
                    }

                    break;
#ifdef INET6
                case DNS_T_AAAA:
                    if (drp->class == DNS_C_IN) {
                        drp->rdlen = IPADDR_MAXLEN + 1;
                        drp->rdata.txt = realloc(drp->rdata.txt, drp->rdlen);
                        inet_ntop(PF_INET6, rdata, drp->rdata.txt, drp->rdlen);
                    }

                    break;
#endif
                case DNS_T_NS:
                case DNS_T_CNAME:
                case DNS_T_PTR:
                    drp->rdlen = DNS_MAX_NAMELEN + 1;
                    drp->rdata.txt = realloc(drp->rdata.txt, drp->rdlen);
                    if ((rc = dn_expand(pkt, pkt + plen, rdata, drp->rdata.txt,
                                    DNS_MAX_NAMELEN + 1)) < 1) {
                        log_error("dn_expand (%d) returned %d", __LINE__, rc);
                        return pidx;
                    }

                    break;
                case DNS_T_SOA:
                    drp->rdlen = sizeof(struct dns_rr_soa);
                    drp->rdata.soa = realloc(drp->rdata.soa, drp->rdlen);

                    if ((rc = dn_expand(pkt, pkt + plen, rdata,
                                    drp->rdata.soa->mname,
                                    DNS_MAX_NAMELEN + 1)) < 1) {
                        log_error("dn_expand (%d) returned %d", __LINE__, rc);
                        return pidx;
                    }
                    sidx += rc;
                    if ((rc = dn_expand(pkt, pkt + plen, rdata + sidx,
                                    drp->rdata.soa->rname,
                                    DNS_MAX_NAMELEN + 1)) < 1) {
                        log_error("dn_expand (%d) returned %d", __LINE__, rc);
                        return pidx;
                    }
                    sidx += rc;
                    memcpy(drp->rdata.txt +
                            offsetof(struct dns_rr_soa, serial), rdata + sidx,
                            sizeof(uint32_t) * 5);
                    drp->rdata.soa->serial = ntohl(drp->rdata.soa->serial);
                    drp->rdata.soa->refresh = ntohl(drp->rdata.soa->refresh);
                    drp->rdata.soa->retry = ntohl(drp->rdata.soa->retry);
                    drp->rdata.soa->expire = ntohl(drp->rdata.soa->expire);
                    drp->rdata.soa->minimum = ntohl(drp->rdata.soa->minimum);

                    break;
                case DNS_T_HINFO:
                    drp->rdlen = sizeof(struct dns_rr_hinfo);
                    drp->rdata.hinfo = realloc(drp->rdata.hinfo, drp->rdlen);

                    sidx = strlcpy(drp->rdata.hinfo->cpu, rdata,
                            DNS_MAX_NAMELEN + 1);
                    strlcpy(drp->rdata.hinfo->os, rdata + sidx + 1,
                            DNS_MAX_NAMELEN + 1);

                    break;
                case DNS_T_MINFO:
                    drp->rdlen = sizeof(struct dns_rr_minfo);
                    drp->rdata.minfo = realloc(drp->rdata.minfo, drp->rdlen);

                    if ((rc = dn_expand(pkt, pkt + plen, rdata,
                                    drp->rdata.minfo->rmailbx,
                                    DNS_MAX_NAMELEN + 1)) < 1) {
                        log_error("dn_expand (%d) returned %d", __LINE__, rc);
                        return pidx;
                    }
                    sidx += rc;
                    if ((rc = dn_expand(pkt, pkt + plen, rdata + sidx,
                                    drp->rdata.minfo->emailbx,
                                    DNS_MAX_NAMELEN + 1)) < 1) {
                        log_error("dn_expand (%d) returned %d", __LINE__, rc);
                        return pidx;
                    }

                    break;
                case DNS_T_MX:
                    drp->rdlen = sizeof(struct dns_rr_mx);
                    drp->rdata.mx = realloc(drp->rdata.mx, drp->rdlen);

                    memcpy(&drp->rdata.mx->preference, rdata,
                            sizeof(uint16_t));
                    drp->rdata.mx->preference =
                        ntohs(drp->rdata.mx->preference);

                    if ((rc = dn_expand(pkt, pkt + plen,
                                    rdata + sizeof(uint16_t),
                                    drp->rdata.mx->exchange,
                                    DNS_MAX_NAMELEN + 1)) < 1) {
                        log_error("dn_expand (%d) returned %d", __LINE__, rc);
                        return pidx;
                    }

                    break;
                case DNS_T_WKS:
                    if (drp->class == DNS_C_IN) {
                        /* hang on to the old data length in rc so we can use
                         * it later. */
                        rc = drp->rdlen;
                        drp->rdlen = sizeof(struct dns_rr_wks);
                        drp->rdata.wks = realloc(drp->rdata.wks, drp->rdlen);

                        /* now copy in the data */
                        inet_ntop(PF_INET, rdata,
                                drp->rdata.wks->address, IPADDR_MAXLEN + 1);
                        sidx += sizeof(struct in_addr);
                        drp->rdata.wks->protocol = *(rdata + sidx);
                        memset(drp->rdata.wks->map, 0, 8192);
                        memcpy(drp->rdata.wks->map, rdata + sidx + 1,
                                rc - sizeof(struct in_addr) - 1);
                    }

                    break;
                default:
                    break;
            }
        }

        if (last_rr == NULL)
            LIST_INSERT_HEAD(list, drp, lp);
        else
            LIST_INSERT_AFTER(last_rr, drp, lp);
        last_rr = drp;
        count--;
    }

    return pidx;
}

/* here we wrap up a lookup.  we move it to the 'cache' list from wherever it
 * might have been and hook its callback event. */
void dns_lookup_finish(dns_lookup_t *dlp) {
    uint32_t minttl = dns.cache.expire;
    struct dns_rr *drp;

    /* Now make an effort to figure out the ttl for this entry.  We use the
     * minimum of the lowest ttl in the answer section (if any) or the SOA (if
     * any) in the answer section or our cache expiry time */
    drp = LIST_FIRST(&dlp->rrs.an);
    while (drp != NULL) {
        if (drp->ttl < minttl)
            minttl = drp->ttl;
        drp = LIST_NEXT(drp, lp);
    }
    if (LIST_EMPTY(&dlp->rrs.an) && !LIST_EMPTY(&dlp->rrs.ns)) {
        /* see if we got an SOA as the first authority section RR .. */
        drp = LIST_FIRST(&dlp->rrs.ns);
        if (drp->type == DNS_T_SOA) {
            if (drp->rdata.soa->minimum < minttl)
                minttl = drp->rdata.soa->minimum;
        }
    }
    
    dlp->last = me.now;
    dlp->ttl = minttl;
    /* call back now.. */
    hook_event(dlp->finished, dlp);

    /* Now add it to the cache if the ttl is non-zero and it is either not a
     * failed lookup or we are cacheing failures. */
    if (dlp->ttl &&
            (dns.cache.failure || !(dlp->flags & DNS_LOOKUP_FL_FAILED))) {
        if (dns.cache.count == dns.cache.max && dns.cache.count > 0)
            destroy_dns_lookup(TAILQ_LAST(&dns.cache.list, dns_lookup_tailq));
        if (dns.cache.max > 0)
            dns_lookup_move(dlp, DNS_LOOKUP_FL_CACHE, true);
    }

    if (!(dlp->flags & DNS_LOOKUP_FL_CACHE)) {
        destroy_dns_lookup(dlp);
        return;
    }

    /* lastly, adjust the timer. */
    if (dlp->timer == TIMER_INVALID)
        /* hmm.. */
        dlp->timer = create_timer(0, dlp->ttl, dns_timer_hook, dlp);
    else
        adjust_timer(dlp->timer, 0, dlp->ttl);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
