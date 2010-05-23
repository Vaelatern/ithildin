/*
 * acl.c: ircd ACL addon code
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * ircd's acl addon provides a system for authorizing connections in several
 * ways.  The general system is documented in doc/ircd/connections.txt
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/acl.h"

IDSTRING(rcsid, "$Id: acl.c 830 2009-01-25 23:08:01Z wd $");

MODULE_REGISTER("$Rev: 830 $");
/*
@DEPENDENCIES@: ircd
*/

struct acl_module_data acl;

/* this is the entry we give to 'default' hashes of ACLs (if the ACL is an IP,
 * CIDR mask, or something else) */
#define ACL_DEFAULT_HASH 0

/* function prototypes */
HOOK_FUNCTION(acl_timer_hook);
HOOK_FUNCTION(acl_conf_hook);
static uint32_t get_acl_hash(const char *);
XINFO_FUNC(xinfo_acl_handler);

/* Create an ACL with the given data.  We will override ACLs that are
 * similar enough to ourself (same stage/host/access/rule#) unless they have
 * special parameter data (such as passwords or 'info line' bans) */
acl_t *create_acl(int stage, int acc, char *host, const char *type, int rule) {
    acl_t *ap, *ap2;
    char *at, hostcopy[ACL_USERLEN + ACL_HOSTLEN + 2];
    struct acl_list *list;
    
    /* look for an ACL in this stage from the same hostname and with the
     * same access and rule number.  If we find one we will delete it unless
     * it has an info-line, in which case we will leave it alone.  (This is
     * sort of a broken concession to not having a create command which
     * takes that information...) */
    if ((ap = find_acl(stage, acc, host, type, rule, NULL, NULL)) != NULL) {
        if (ap->info != NULL)
            ap = NULL;
    }

    /* if we found a match, nuke it and replace it with this new one.  it
     * doesn't make sense to combine stale data, and it could be very
     * disastrous. :/ */
    destroy_acl(ap);

    ap = calloc(1, sizeof(acl_t));
    ap->stage = stage;
    ap->access = acc;
    strlcpy(hostcopy, host, ACL_USERLEN + ACL_HOSTLEN + 2);
    at = strchr(hostcopy, '@');
    if (at == NULL)
        strlcpy(ap->host, hostcopy, ACL_HOSTLEN + 1);
    else {
        *at = '\0';
        strlcpy(ap->user, hostcopy, ACL_USERLEN + 1);
        strlcpy(ap->host, at + 1, ACL_HOSTLEN + 1);
    }
    ap->type = strdup(type);
    ap->cls = LIST_FIRST(ircd.lists.classes); /* point to the default class */
    ap->added = me.now;
    ap->timer = TIMER_INVALID;
    
    /* add them into the big list... */
    if (rule == ACL_DEFAULT_RULE)
        ap->rule = acl.default_rule;
    else
        ap->rule = (short)rule;
    ap->hash = get_acl_hash(ap->host);

    /* insert into lists.. */
    LIST_INSERT_HEAD(acl.list, ap, lp);
    if (ap->stage == ACL_STAGE_CONNECT)
        list = acl.stage1_list;
    else if (ap->stage == ACL_STAGE_PREREG)
        list = acl.stage2_list;
    else
        list = acl.stage3_list;

    if (LIST_EMPTY(list))
        LIST_INSERT_HEAD(list, ap, intlp);
    else {
        LIST_FOREACH(ap2, list, intlp) {
            if (ap2->rule > ap->rule) {
                LIST_INSERT_BEFORE(ap2, ap, intlp);
                break;
            } else if (LIST_NEXT(ap2, intlp) == NULL) {
                LIST_INSERT_AFTER(ap2, ap, intlp);
                break;
            }
        }
    }

    return ap;
}

/* hash 20 characters of the host.  maybe we should do more?  maybe we should
 * just do the domain.tld? */
#define ACL_HASHLEN 20

/* this function takes a regular hostname and returns a pointer to a statically
 * allocated buffer which gives the host's hash-key for the purposes of the ACL
 * system. */
static uint32_t get_acl_hash(const char *host) {
    const char *s;
    int i = 0;
    int dc = 0;
    uint32_t hash = 0;

    /* figure out what 'hv' in the structure should be */
    s = host + strlen(host) - 1; /* point s at the last char */
    while (s > host && i < ACL_HASHLEN) {
        if (*s == '.') {
            /* check for periods.  if we hit a second one, step forward one
             * character and hash from there.  this means we always hash
             * 'domain.tld' and not more (but maybe less for long domains) */
            dc++;
            if (dc > 1) {
                s++; /* go forward again */
                break;
            }
        }
        s--;
        i++;
    }

    /* now s is the entry to hash */
    if (strlen(s) < 5 || !istr_okay(ircd.maps.host, s)) {
        /* if s is shorter than five bytes, or if it contains an invalid
         * character (something like *, @, /, whatever) we give up on hashing
         * it. */
        return ACL_DEFAULT_HASH;
    }

    /* now hash it */
    while (*s != '\0')
        hash = hash * 33 + tolower(*s++);

    return hash + (hash >> 5);
}

/* this function finds an ACL based on stage/host/type, and possibly based on
 * the pass/info parameters. */
acl_t *find_acl(int stage, int acc, char *hostmask, const char *type,
        int rule, char *pass, char *info) {
    struct acl_list *list;
    char *at, hostcopy[ACL_USERLEN + ACL_HOSTLEN + 2], user[ACL_USERLEN + 1];
    char host[ACL_HOSTLEN + 1];
    acl_t *ap;

    
    /* extract user@host data */
    strlcpy(hostcopy, hostmask, ACL_USERLEN + ACL_HOSTLEN + 2);
    at = strchr(hostcopy, '@');
    if (at == NULL) {
        *user = '\0';
        strlcpy(host, hostcopy, ACL_HOSTLEN + 1);
    } else {
        *at = '\0';
        strlcpy(user, hostcopy, ACL_USERLEN + 1);
        strlcpy(host, at + 1, ACL_HOSTLEN + 1);
    }

    if (rule == ACL_DEFAULT_RULE)
        rule = acl.default_rule;

    if (stage == ACL_STAGE_CONNECT)
        list = acl.stage1_list;
    else if (stage == ACL_STAGE_PREREG)
        list = acl.stage2_list;
    else
        list = acl.stage3_list;

    /* now try and find them in the bucket. */
    LIST_FOREACH(ap, list, intlp) {
        if (acc != ACL_ACCESS_ANY && ap->access != acc)
            continue;
        if (rule != ACL_ANY_RULE && ap->rule != rule)
            continue;

        if (!strcasecmp(ap->user, user) && !strcasecmp(ap->host, host) &&
                !strcasecmp(ap->type, type) &&
                (pass == NULL || !strcmp(ap->pass, pass)) &&
                (info == NULL || !strcmp(ap->info, info)))
            return ap;
    }

    return NULL;
}

/* remove an ACL from the requisite lists and release its memory */
void destroy_acl(acl_t *ap) {

    if (ap == NULL)
        return; /* umm. :) */

    /* remove it from the big list */
    LIST_REMOVE(ap, lp);
    
    /* and remove it from whatever list it's in */
    if (ap->stage == ACL_STAGE_CONNECT)
        LIST_REMOVE(ap, intlp);
    else if (ap->stage == ACL_STAGE_PREREG)
        LIST_REMOVE(ap, intlp);
    else
        LIST_REMOVE(ap, intlp);

    /* now free the memory */
    if (ap->type != NULL)
        free(ap->type);
    if (ap->reason != NULL)
        free(ap->reason);
    if (ap->pass != NULL)
        free(ap->pass);
    if (ap->info != NULL)
        free(ap->info);
    if (ap->redirect != NULL)
        free(ap->redirect);
    if (ap->timer != TIMER_INVALID)
        destroy_timer(ap->timer);
    free(ap);

    return;
}

/* if necessary, add a timer for an acl entry. */
void acl_add_timer(acl_t *ap, time_t expire) {

    if (ap->timer != TIMER_INVALID)
        destroy_timer(ap->timer); /* kill the old one.. */
    ap->expire = expire;
    ap->timer = create_timer(0, ap->expire, acl_timer_hook, ap);
}

/* force a check of every client in a specific stage (or all stages) against
 * the current acl list and return the number of clients affected (read:
 * removed).  If msg_always is true a message will be sent even if no clients
 * are effected.  Most of the info is pulled from the acl passed in, 'by' is a
 * string specifying who added the acl. */
void acl_force_check(int stage, const acl_t *ap, const char *by, bool
        msg_always) {
    int nukes = 0;
    int checks = 0;
    connection_t *cp, *cp2;
    void *ret;

    if (stage == 0 || stage == ACL_STAGE_CONNECT) {
        cp = LIST_FIRST(ircd.connections.stage1);
        while (cp != NULL) {
            cp2 = LIST_NEXT(cp, lp);
            if ((ret = acl_stage1_hook(NULL, (void *)cp)) != NULL) {
                destroy_connection(cp, ret);
                nukes++;
            }
            checks++;
            cp = cp2;
        }
    }
    if (stage == 0 || stage == ACL_STAGE_PREREG) {
        cp = LIST_FIRST(ircd.connections.stage2);
        while (cp != NULL) {
            cp2 = LIST_NEXT(cp, lp);
            if ((ret = acl_stage2_hook(NULL, (void *)cp)) != NULL) {
                destroy_connection(cp, ret);
                nukes++;
            }
            checks++;
            cp = cp2;
        }
    }
    if (stage == 0 || stage == ACL_STAGE_REGISTER) {
        cp = LIST_FIRST(ircd.connections.clients);
        while (cp != NULL) {
            cp2 = LIST_NEXT(cp, lp);
            if ((ret = acl_stage3_hook(NULL, (void *)cp)) != NULL) {
                destroy_connection(cp, ret);
                nukes++;
            }
            checks++;
            cp = cp2;
        }
    }

    /* spam a message (maybe?) */
    if (msg_always || nukes)
        sendto_flag(ircd.sflag.ops, "%s added %s for %s%s%s "
                "(stage %d, access %s) (%d killed, %d checked) [%s]",
                by, ap->type,
                (ap->stage > 1 ? ap->user : ""), (ap->stage > 1 ? "@" : ""),
                ap->host, ap->stage,
                (ap->access == ACL_DENY ? "DENY" :
                 (ap->access == ACL_ALLOW ? "ALLOW" : "UNKNOWN")),
                nukes, checks,
                (ap->reason != NULL && *ap->reason ? ap->reason :
                 "No Reason"));
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
/* handling for stage one is by far the easiest, there are no classes or
 * passwords involved.  also, stuff in stage1 should always be in a single
 * place.  we create a hashtable, but it's mostly just a glorified list (for
 * now, anyways) */
HOOK_FUNCTION(acl_stage1_hook) {
    acl_t *ap;
    connection_t *cp = (connection_t  *)data;
    uint32_t hash = get_acl_hash(cp->host);

    /* now match.  skip over any entries that aren't the default hash or our
     * hash. */
    LIST_FOREACH(ap, acl.stage1_list, intlp) {
        if (ap->hash != hash && ap->hash != ACL_DEFAULT_HASH)
            continue;

        if (ipmatch(ap->host, cp->host) ||
                hostmatch(ap->host, cp->host)) {
            if (ap->access == ACL_DENY)
                return (ap->reason != NULL ? ap->reason : "");
            else
                break; /* accepted, do work below. */
        }
    }

    if (ap != NULL) {
        if (ap->flags & ACL_FL_SKIP_DNS)
            cp->flags |= IRCD_CONNFL_DNS;
        if (ap->flags & ACL_FL_SKIP_IDENT)
            cp->flags |= IRCD_CONNFL_IDENT;
    }

    return NULL; /* default to accept */
}

/* stage2 is next, and resembles stage1 except that we do class checks, and we
 * actually have a full mask now. */
HOOK_FUNCTION(acl_stage2_hook) {
    acl_t *ap;
    connection_t  *cp = (connection_t  *)data;
    uint32_t hash, iphash;
    char ip[ACL_HOSTLEN + 1];

    get_socket_address(isock_raddr(cp->sock), ip, ACL_HOSTLEN + 1, NULL);

    hash = get_acl_hash(cp->host);
    iphash = get_acl_hash(ip);

    /* this is more complicated now.  we must check the username, the hostname,
     * and the ip (using hostmatch and ipmatch).  All together that is four
     * calls.  Also, our hash space is expanded. */
    LIST_FOREACH(ap, acl.stage2_list, intlp) {
        if (ap->hash != hash && ap->hash != iphash &&
                ap->hash != ACL_DEFAULT_HASH)
            continue; /* skip .. */

        if ((*ap->user ? hostmatch(ap->user, cp->user) : 1)) {
            if (hostmatch(ap->host, cp->host) ||
                    hostmatch(ap->host, ip) || ipmatch(ap->host, ip)) {
                /* okay, it actually matches. */
                if (ap->access == ACL_DENY)
                    return (ap->reason != NULL ? ap->reason : "");
                else if (ap->cls->max > ap->cls->clients) {
                    /* don't forget to put them in the connection class if
                     * specified unless we were called manually. */
                    if (ep != NULL)
                        add_to_class(ap->cls, cp);
                    return NULL;
                }
            }
        }
    }

    return NULL; /* default to accept */
}

/* stage3 is the last stage, it's nearly identical to stage2, but there is a
 * slight change for 'allow-always' type rules and passwords, also, unlike the
 * previous two, the default is to *deny* clients.  Also, unlike the other two
 * stages, while the first match *usually* wins, we do check for 'allow-always'
 * rules. */
HOOK_FUNCTION(acl_stage3_hook) {
    acl_t *ap;
    connection_t  *cp = (connection_t  *)data;
    uint32_t hash, iphash;
    char ip[ACL_HOSTLEN + 1];
    void *ret = NULL;

    get_socket_address(isock_raddr(cp->sock), ip, ACL_HOSTLEN + 1, NULL);

    hash = get_acl_hash(cp->host);
    iphash = get_acl_hash(ip);

    /* this is the most complicated search.  we need to check for passwords,
     * info/gecos, and user/host/ip.  also, in the case of 'allows', if they
     * aren't allowed because of a class being full we actually have to keep on
     * looking to see if they fit in somewhere else. blechhh. */
    LIST_FOREACH(ap, acl.stage3_list, intlp) {
        if (ap->hash != hash && ap->hash != iphash &&
                ap->hash != ACL_DEFAULT_HASH)
            continue; /* don't bother.. */
        
        /* check password/info first, since they're cheaper than all the
         * match calls (I guess */
        if (ap->pass != NULL && cp->pass != NULL && strcmp(ap->pass, cp->pass))
            continue; /* password incorrect */
        if (ap->info != NULL && !match(ap->info, cp->cli->info))
            continue; /* info doesn't match */
        if ((*ap->user ? hostmatch(ap->user, cp->cli->user) : 1)) {
            if (hostmatch(ap->host, cp->host) ||
                hostmatch(ap->host, ip) || ipmatch(ap->host, ip)) {
                /* okay, it actually matches. */
                if (ap->access == ACL_DENY) {
                    /* Is this a redirect?  Maybe so, let's send them the
                     * redirect message if it is. */
                    if (ap->redirect != NULL)
                        sendto_one(cp->cli, RPL_FMT(cp->cli, RPL_REDIR),
                                ap->redirect, ap->redirect_port);

                    /* if there's a reason for the ban and they weren't already
                     * denied set ret, otherwise just leave it alone.  Why?
                     * this is possible if there is no more room in their
                     * connection class.  We want them to know that they
                     * would be authorized if the class wasn't full, instead
                     * of telling them they are not at all authorized. */
                    ret = (ret != NULL ? ret : ap->reason);
                    break; /* we'll return below. */
                } else {
                    /* if it's an accept, check to see if the class has
                     * room.  if it does, or we are always accepting,
                     * return immediately.  if ep is NULL we were called
                     * manually, so no class change should occur. */
                    if (ap->cls->max < 1 || ap->cls->max > ap->cls->clients) {
                        if (ep != NULL)
                            add_to_class(ap->cls, cp);
                        return NULL;
                    } else
                        ret = "No more clients allowed in your "
                           "connection class (the server is full)";
                }
            }
        }
    }

    if (ret == NULL && !LIST_EMPTY(acl.stage3_list))
        return "You are not authorised to use this server.";
    return ret;
}

HOOK_FUNCTION(acl_timer_hook) {
    acl_t *ap = (acl_t *)data;

    ap->timer = TIMER_INVALID;
    destroy_acl(ap);

    return NULL;
}

/* These two are the defaults for runtime and configured rule numbers,
 * respectively. */
#define ACLCONF_DEFAULT_RULE 1000
#define ACLCONF_DEFAULT_CONF_RULE 2000
HOOK_FUNCTION(acl_conf_hook) {
    conf_entry_t *cep;
    conf_list_t *clp;
    acl_t *ap, *ap2;
    int stg, acc, redirect_port = 0;
    char *s, *class, *pass, *info, *reason, *redir;
    char redirect[SERVLEN + 1];
    class_t *cls;
    int rule;
    int default_rule; /* default rule for config entries */

    /* remove anything that points to a conf.  usually this will only be
     * ACLs from the actual config, but anything that wants to let ACLs get
     * wiped out by a /rehash can point at 0x1 or whatever */
    ap = LIST_FIRST(acl.list);
    while (ap != NULL) {
        ap2 = LIST_NEXT(ap, lp);
        if (ap->conf != NULL)
            destroy_acl(ap);
        ap = ap2;
    }

    /* see about setting the default rule number.. */
    if ((s = conf_find_entry("default-acl-rule", *ircd.confhead, 1)) != NULL)
        acl.default_rule = str_conv_int(s, ACLCONF_DEFAULT_RULE);
    else
        acl.default_rule = ACLCONF_DEFAULT_RULE;
    if ((s = conf_find_entry("default-acl-conf-rule", *ircd.confhead, 1)) !=
            NULL)
        default_rule = str_conv_int(s, ACLCONF_DEFAULT_CONF_RULE);
    else
        default_rule = ACLCONF_DEFAULT_CONF_RULE;

    /* now read through the conf looking for ACLs, as we find them parse and
     * add them.  we may tweak 'default_rule' here, too.  Oh yeah, also, we
     * need to iterate through each acl multiple times to support multiple
     * hosts per acl.  Additionally, support hostlists.  Whew. */
    cep = NULL;
    while ((cep = conf_find_next("acl", NULL, CONF_TYPE_LIST, cep,
                    *ircd.confhead, 1)) != NULL) {
        rule = default_rule;
        if (cep->string != NULL)
            rule = str_conv_int(cep->string, -1);
        if (rule < 0 || rule > USHRT_MAX) {
            log_warn("got acl with bogus rule number (%d)", rule);
            rule = default_rule;
        }

        clp = cep->list;
        stg = str_conv_int(conf_find_entry("stage", clp, 1),
                ACL_STAGE_REGISTER);
        s = conf_find_entry("access", clp, 1);
        if (s == NULL) {
            log_warn("ACL has no access field.");
            continue;
        } else {
            if (!strcasecmp(s, "allow"))
                acc = ACL_ALLOW;
            else if (!strcasecmp(s, "deny"))
                acc = ACL_DENY;
            else {
                acc = ACL_DENY;
                log_warn("ACL has unknown access type %s, "
                        "defaulting to deny.", s);
            }
        }

        /* find all of our supplemental data before doing the add loop .. */
        class = pass = info = reason = NULL;
        cls = LIST_FIRST(ircd.lists.classes);
        if ((class = conf_find_entry("class", clp, 1)) != NULL) {
            cls = find_class(class);
            if (cls == NULL || cls->dead) {
                log_warn("could not find class %s or it is marked dead",
                        class);
                cls = LIST_FIRST(ircd.lists.classes);
            }
        }
        pass = conf_find_entry("pass", clp, 1);
        info = conf_find_entry("info", clp, 1);
        if (acc == ACL_DENY) {
            if ((reason = conf_find_entry("reason", clp, 1)) == NULL)
                reason = "You are not authorised to use this server.";
        }

        if ((redir = conf_find_entry("redirect", clp, 1)) != NULL) {
            /* try finding the port .. */
            if ((s = strchr(redir, ':')) != NULL) {
                strlcpy(redirect, redir, (s - redir <= SERVLEN ?
                            (s - redir) + 1 : SERVLEN + 1));
                if ((redirect_port = str_conv_int(s + 1, 0)) == 0)
                    log_warn("Could not parse server:port combo for redirect "
                            "%s", redir);
            } else {
                strlcpy(redirect, redir, SERVLEN + 1);
                redirect_port = 6667;
            }

            if (acc != ACL_DENY) {
                log_warn("Redirection forces a DENY ACL type.");
                acc = ACL_DENY;
            }
        } else
            *redirect = '\0';
                


        /* now iterate through all the host lists and all the regular hosts and
         * add the acl.  yikes!  Use this macro to make life a bit easier. */
#define ACL_PARSE_ADD(_host) do {                                             \
        ap = create_acl(stg, acc, _host, "acl", rule);                        \
        ap->conf = clp;                                                       \
        ap->cls = cls;                                                        \
        if (pass != NULL)                                                     \
            ap->pass = strdup(pass);                                          \
        if (info != NULL)                                                     \
            ap->info = strdup(info);                                          \
        if (reason != NULL)                                                   \
            ap->reason = strdup(reason);                                      \
        if (str_conv_bool(conf_find_entry("skip-dns", clp, 1), 0))            \
            ap->flags |= ACL_FL_SKIP_DNS;                                     \
        if (str_conv_bool(conf_find_entry("skip-ident", clp, 1), 0))          \
            ap->flags |= ACL_FL_SKIP_IDENT;                                   \
        if (*redirect != '\0' && redirect_port != 0) {                        \
            ap->redirect = strdup(redirect);                                  \
            ap->redirect_port = redirect_port;                                \
        }                                                                     \
} while (0)

        /* first do individual hosts, then host-lists. */
        s = NULL;
        while ((s = conf_find_entry_next("host", s, clp, 1)) != NULL)
            ACL_PARSE_ADD(s);
        while ((s = conf_find_entry_next("host-list", s, clp, 1)) != NULL) {
            hostlist_t *hlp = find_host_list(s);
            int i;
            
            if (hlp == NULL) {
                log_warn("host-list %s does not exist.", s);
                continue;
            }
            for (i = 0;i < hlp->entries;i++)
                ACL_PARSE_ADD(hlp->list[i]);
        }
#undef ACL_PARSE_ADD
    }
    return NULL;
}

XINFO_FUNC(xinfo_acl_handler) {
    /* the acl handler has a somewhat complex query language.  you can query
     * for ACLs based on four parameters: mask, stage, access, and type. */
    acl_t *ap;
    struct acl_list *list;
    char rpl[XINFO_LEN];
    char *mask = NULL, *type = NULL;
    char *user, *host;
    int acc = -1;
    int stage = 0;

    if (argc > 1) {
        /* argv[1] contains our query.  we have to parse it out, which is a bit
         * of a pain. */
        char *buf = argv[1];
        char *s, *tok = NULL;

        while ((s = strsep(&buf, " \t")) != NULL) {
            if (*s == '\0')
                continue;
            if (tok == NULL)
                tok = s;
            else {
                /* we got a token last time, now see what they specified it as
                 * and handle the data. */
                if (!strcasecmp(tok, "mask"))
                    mask = s;
                else if (!strcasecmp(tok, "stage")) {
                    stage = str_conv_int(s, 0);
                    if (stage < ACL_STAGE_CONNECT ||
                            stage > ACL_STAGE_REGISTER) {
                        snprintf(rpl, XINFO_LEN,
                                "%s is not a valid stage type", s);
                        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "ERROR", rpl);
                        return;
                    }
                } else if (!strcasecmp(tok, "access")) {
                    if (!strcasecmp(s, "deny"))
                        acc = ACL_DENY;
                    else if (!strcasecmp(s, "allow"))
                        acc = ACL_ALLOW;
                    else {
                        snprintf(rpl, XINFO_LEN,
                                "%s is not a valid access type", s);
                        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "ERROR", rpl);
                        return;
                    }
                } else if (!strcasecmp(tok, "type"))
                    type = s;
                tok = NULL; /* be sure to set this.. */
            }
        }
    }

    user = host = NULL;
    if (mask != NULL && *mask != '\0') {
        if ((host = strchr(mask, '@')) != NULL) {
            user = mask;
            *host++ = '\0'; /* user@host */
        } else if ((host = strpbrk(mask, ".:")))
            host = mask;
        /* non-sensical mask? */
    }

    /* okay, now dump them the list.. */
    switch (stage) {
        case ACL_STAGE_PREREG:
            list = acl.stage2_list;
            break;
        case ACL_STAGE_REGISTER:
            list = acl.stage3_list;
            break;
        case ACL_STAGE_CONNECT:
        default:
            list = acl.stage1_list;
    }
    while (list != NULL) {
        LIST_FOREACH(ap, list, intlp) {
            if (acc != -1 && ap->access != acc)
                continue;
            if (user != NULL && !match(user, ap->user))
                continue;
            if (host != NULL && !match(host, ap->host))
                continue;
            if (type != NULL && !strcasecmp(ap->type, type))
                continue;
            snprintf(rpl, XINFO_LEN,
                    "STAGE %d RULE %5hu ADDRESS %s%s%s ACCESS %s TYPE %s",
                    ap->stage, ap->rule,
                    (*ap->user ? ap->user : ""), (*ap->user ? "@" : ""),
                    ap->host, (ap->access == ACL_DENY ? "DENY" :
                     (ap->access == ACL_ALLOW ? "ALLOW" : "UNKNOWN")),
                    ap->type);
            if (ap->expire != 0) {
                strlcat(rpl, " LENGTH ", XINFO_LEN);
                strlcat(rpl, time_conv_str(ap->expire), XINFO_LEN);
            }
            if (ap->info != NULL) {
                strlcat(rpl, " INFO ", XINFO_LEN);
                strlcat(rpl, ap->info, XINFO_LEN);
            }
            if (ap->reason != NULL) {
                strlcat(rpl, " REASON ", XINFO_LEN);
                strlcat(rpl, ap->reason, XINFO_LEN);
            }
            sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "ACL", rpl);
        }

        /* keep looping? */
        if (stage == 0) {
            if (list == acl.stage1_list)
                list = acl.stage2_list;
            else if (list == acl.stage2_list)
                list = acl.stage3_list;
            else
                break;
        } else
            break;
    }
}

MODULE_LOADER(acl) {

    memset(&acl, 0, sizeof(acl));

    LIST_ALLOC(acl.list);
    LIST_ALLOC(acl.stage1_list);
    LIST_ALLOC(acl.stage2_list);
    LIST_ALLOC(acl.stage3_list);

    add_xinfo_handler(xinfo_acl_handler, "ACL", XINFO_HANDLER_OPER, 
            "Provides information about the server Access Control List");

    /* add hooks for stage checks */
    add_hook(ircd.events.stage1_connect, acl_stage1_hook);
    add_hook(ircd.events.stage2_connect, acl_stage2_hook);
    add_hook(ircd.events.stage3_connect, acl_stage3_hook);
    add_hook(me.events.read_conf, acl_conf_hook);

        acl_conf_hook(NULL, NULL);

    return 1;
}
MODULE_UNLOADER(acl) {

    while (!LIST_EMPTY(acl.list))
        destroy_acl(LIST_FIRST(acl.list));
    LIST_FREE(acl.list);
    LIST_FREE(acl.stage1_list);
    LIST_FREE(acl.stage2_list);
    LIST_FREE(acl.stage3_list);

    remove_xinfo_handler(xinfo_acl_handler);

    remove_hook(ircd.events.stage1_connect, acl_stage1_hook);
    remove_hook(ircd.events.stage2_connect, acl_stage2_hook);
    remove_hook(ircd.events.stage3_connect, acl_stage3_hook);
    remove_hook(me.events.read_conf, acl_conf_hook);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
