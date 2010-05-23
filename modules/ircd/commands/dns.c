/*
 * dns.c: the DNS command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * This command acts as a 'dig' like command using the dns module to perform
 * lookups on various targets in the DNS
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "../../dns/dns.h"
#include "../../dns/lookup.h"

IDSTRING(rcsid, "$Id: dns.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: dns
*/

struct dns_cmd_lookup {
    char    client[NICKLEN + 1];        /* the client we're doing this for */
    time_t  started;                        /* when the lookup started */
    dns_lookup_t *lookup;                /* and the lookup being done */

    LIST_ENTRY(dns_cmd_lookup) lp;
};
static LIST_HEAD(, dns_cmd_lookup) dns_cmd_lookups;

HOOK_FUNCTION(dns_cmd_lookup_hook);

/* The dns command.  It takes the arguments [host] [type] [class].  If host is
 * not specified then statistics of the dns module (cache entries and such) are
 * sent.  If host is specified a lookup is performed using the type and class
 * specified (or A and IN respectively) */
CLIENT_COMMAND(dns, 0, 3, COMMAND_FL_OPERATOR) {
    struct dns_cmd_lookup *dclp;
    dns_class_t cls = DNS_C_IN;
    dns_type_t typ = DNS_T_A;

    if (argc < 2) {
        dns_lookup_t *dlp;
        /* no arguments ... send data */
        sendto_one(cli, "NOTICE", ":%d/%d lookups (active/max) timeout %s",
                dns.pending.acount, dns.pending.max,
                time_conv_str(dns.pending.timeout));
        sendto_one(cli, "NOTICE", ":%d/%d lookups (cached/max) expire %s",
                dns.cache.count, dns.cache.max,
                time_conv_str(dns.cache.expire));
        TAILQ_FOREACH(dlp, &dns.cache.list, lp)
            sendto_one(cli, "NOTICE", ":cached: %s (%s %s) expire %s status %s",
                    dlp->data, dns_class_conv_str(dlp->class),
                    dns_type_conv_str(dlp->type),
                    time_conv_str(dlp->ttl - (me.now - dlp->last)),
                    (dlp->flags & DNS_LOOKUP_FL_FAILED ? "failed" : "okay"));

        return COMMAND_WEIGHT_NONE;
    }
    if (argc > 2) {
        typ = dns_str_conv_type(argv[2]);
        if (dns_type_conv_str(typ) == NULL) {
            sendto_one(cli, "NOTICE", ":%s is not a valid zone type", argv[2]);
            return COMMAND_WEIGHT_NONE;
        }
    }
    if (argc > 3) {
        cls = dns_str_conv_class(argv[3]);
        if (dns_class_conv_str(cls) == NULL) {
            sendto_one(cli, "NOTICE", ":%s is not a valid zone type", argv[3]);
            return COMMAND_WEIGHT_NONE;
        }
    }
        
    dclp = malloc(sizeof(struct dns_cmd_lookup));
    strcpy(dclp->client, cli->nick);
    dclp->started = me.now;
    dclp->lookup = NULL;
    LIST_INSERT_HEAD(&dns_cmd_lookups, dclp, lp);

    /* the lookup call may actually hook before even returning, so be careful
     * here..  it will however return a pointer of some sort, even if that
     * pointer might not be valid, and only return NULL on error */
    if ((dclp->lookup = dns_lookup(cls, typ, argv[1], dns_cmd_lookup_hook)) ==
            NULL) {
        sendto_one(cli, "NOTICE", ":Lookup for %s (%s %s) failed!", argv[1],
                dns_class_conv_str(cls), dns_type_conv_str(typ));
        LIST_REMOVE(dclp, lp);
        free(dclp);
    }

    return COMMAND_WEIGHT_NONE;
}

HOOK_FUNCTION(dns_cmd_lookup_hook) {
    struct dns_cmd_lookup *dclp;
    dns_lookup_t *dlp = (dns_lookup_t *)data;
    struct dns_rr *drp;
    client_t *cli;

    /* if the lookup is NULL assume we're getting hooked from within the
     * dns_lookup() function. */
    LIST_FOREACH(dclp, &dns_cmd_lookups, lp) {
        if (dclp->lookup == NULL || dclp->lookup == dlp)
            break; /* got it */
    }

    if (dclp == NULL) {
        log_error("Got dns lookup without associated record!");
        return NULL; /* this should never happen */
    }

    if ((cli = find_client(dclp->client)) == NULL ||
        dclp->started < cli->signon) {
        LIST_REMOVE(dclp, lp);
        free(dclp);
        return NULL; /* do nothing in this case */
    }


    /* okay, we know the client is the right one and we have all the lookup
     * details too. */
    sendto_one(cli, "NOTICE", ":Lookup for %s (%s %s) took %s", dlp->data,
            dns_class_conv_str(dlp->class), dns_type_conv_str(dlp->type),
            time_conv_str(me.now - dclp->started));
    if (dlp->flags & DNS_LOOKUP_FL_CACHE)
        sendto_one(cli, "NOTICE", ":Cached for %s",
                time_conv_str(dlp->ttl - (me.now - dlp->last)));

    /* go ahead and get rid of dclp, we don't need it anymore */
    LIST_REMOVE(dclp, lp);
    free(dclp);

    /* check for errors ... */
    if (dlp->flags & DNS_LOOKUP_FL_FAILED) {
        if (dlp->flags & DNS_LOOKUP_FL_NXDOMAIN)
            sendto_one(cli, "NOTICE", ":Non-existant domain");
        else if (dlp->flags & DNS_LOOKUP_FL_TIMEOUT)
            sendto_one(cli, "NOTICE", ":Lookup timed out");
        else
            sendto_one(cli, "NOTICE", ":Lookup failed for unknown reasons.");
        return NULL;
    }
        
    /* now walk through the three sections and send info on each */
    sendto_one(cli, "NOTICE", "Answer section:");
    LIST_FOREACH(drp, &dlp->rrs.an, lp)
        sendto_one(cli, "NOTICE", ":%s", dns_lookup_pretty_rr(drp));
    sendto_one(cli, "NOTICE", "Authority section:");
    LIST_FOREACH(drp, &dlp->rrs.ns, lp)
        sendto_one(cli, "NOTICE", ":%s", dns_lookup_pretty_rr(drp));
    sendto_one(cli, "NOTICE", "Additional section:");
    LIST_FOREACH(drp, &dlp->rrs.ad, lp)
        sendto_one(cli, "NOTICE", ":%s", dns_lookup_pretty_rr(drp));

    /* All done */
    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
