/*
 * conf.c: ircd configuration data parser
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file provides the necessary functions for reading, and passing to the
 * correct subsystems, configuration data in the 'ircd.conf' file.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: conf.c 850 2010-04-30 02:29:46Z wd $");

static void ircd_parse_addons(conf_list_t *conf);
static void ircd_parse_classes(conf_list_t *conf);
static void ircd_parse_commands(conf_list_t *conf);
static void ircd_parse_global(conf_list_t *conf);
static void ircd_parse_message_sets(conf_list_t *conf);
static void ircd_parse_privilege_sets(conf_list_t *conf);
static void ircd_parse_ports(conf_list_t *conf);
static void ircd_add_listener(struct isocket_list *, char *, unsigned short,
        bool);
static void ircd_parse_protocols(conf_list_t *conf);
static void ircd_parse_servers(conf_list_t *conf);
static void ircd_parse_host_lists(conf_list_t *conf);

/* this is the function which handles the top-level parsing of the
 * configuration file.  by and large it simply looks for other
 * sections/directives and passes them off to handler functions.  the most
 * important thing to note is that any required section should be looked for
 * *and errored on if not found* before one attempts to parse it, otherwise
 * the system may load modules and not clean them up */
int ircd_parse_conf(conf_list_t *conf) {
    conf_list_t *tmp;
    char *ent;
    /* required sections here */
    conf_list_t *global, *protocols, *commands;

#define PARSE_ERRCHK(x)                                                    \
    if (x == NULL) {                                                       \
        log_error("your ircd configuration file contains no " #x " "       \
                "section.  This section is required for the module to "    \
                "operate correctly.");                                     \
        return 0;                                                          \
    }

    /* quickly find all our required sections */
    global = conf_find_list("global", conf, 1);
    PARSE_ERRCHK(global);
    ircd_parse_protocols(conf);
    protocols = conf_find_list("protocols", conf, 1);
    PARSE_ERRCHK(protocols);
    commands = conf_find_list("commands", conf, 1);
    PARSE_ERRCHK(commands);
#undef PARSE_ERRCHK

    /* now parse our sections.  huzz-ah */
    ircd_parse_global(global);

    ircd_parse_message_sets(conf); /* keep this BEFORE classes */
    ircd_parse_privilege_sets(conf); /* this too */
    /* add a quick check to see that we have at least one message/priv set */
    if (LIST_FIRST(ircd.messages.sets) == NULL) {
        log_error("you must include at least one message set!");
        return 0;
    }
    if (LIST_FIRST(ircd.privileges.sets) == NULL) {
        log_error("you must include at least one privileges set!");
        return 0;
    }
    ircd_parse_classes(conf);
    if (LIST_FIRST(ircd.lists.classes) == NULL) {
        log_error("you must include at least one connection class!");
        return 0;
    }
    ircd_parse_servers(conf);
    ircd_parse_host_lists(conf);

    /* now do all the stuff that loads modules */
    ircd_parse_protocols(protocols);
    ircd_parse_addons(conf);
    ircd_parse_commands(commands);

    /* read in character tables, if provided */
    tmp = conf_find_list("charmaps", conf, 1);
    if (tmp != NULL) {
        ent = conf_find_entry("nick", tmp, 1);
        if (ent != NULL)
            if (!istr_map(ent, ircd.maps.nick))
                log_warn("invalid character map pattern \"%s\"", ent);
        ent = conf_find_entry("nick_first", tmp, 1);
        if (ent != NULL)
            if (!istr_map(ent, ircd.maps.nick_first))
                log_warn("invalid character map pattern \"%s\"", ent);
        ent = conf_find_entry("channel", tmp, 1);
        if (ent != NULL)
            if (!istr_map(ent, ircd.maps.channel))
                log_warn("invalid character map pattern \"%s\"", ent);
        ent = conf_find_entry("host", tmp, 1);
        if (ent != NULL)
            if (!istr_map(ent, ircd.maps.host))
                log_warn("invalid character map pattern \"%s\"", ent);
    }

    return 1;
}

static void ircd_parse_global(conf_list_t *conf) {
    conf_list_t *ctmp;
    char *stmp;

    /* first, grab one-offs.  some of these only ever apply when ircd is
     * unstarted (for instance, you cannot change your server name with a
     * /rehash ;) */
    if (!ircd.started) {
        stmp = conf_find_entry("name", conf, 1);
        if (stmp != NULL)
            strncpy(ircd.me->name, stmp, SERVLEN);
        else
            strcpy(ircd.me->name, "a.nameless.server");
        stmp = conf_find_entry("address", conf, 1);
        if (stmp != NULL)
            strncpy(ircd.address, stmp, HOSTLEN);
        else
            strncpy(ircd.address, "0.0.0.0", HOSTLEN);
        stmp = conf_find_entry("nicklen", conf, 1);
        if (stmp != NULL) {
            ircd.limits.nicklen = str_conv_int(stmp, 0);
            if (ircd.limits.nicklen > NICKLEN) {
                log_warn("you cannot set nicklen > %d without recompiling.",
                        NICKLEN);
                log_warn("setting nicklen from %d to %d", ircd.limits.nicklen,
                        NICKLEN);
                ircd.limits.nicklen = NICKLEN;
            } else if (ircd.limits.nicklen < 9)  {
                log_warn("you cannot set nicklen < 9.");
                log_warn("setting nicklen from %d to %d", ircd.limits.nicklen,
                        NICKLEN);
                ircd.limits.nicklen = NICKLEN;
            }
        }
        stmp = conf_find_entry("chanlen", conf, 1);
        if (stmp != NULL) {
            ircd.limits.chanlen = str_conv_int(stmp, 0);
            if (ircd.limits.chanlen > CHANLEN) {
                log_warn("you cannot set chanlen > %d without recompiling.",
                        CHANLEN);
                log_warn("setting chanlen from %d to %d", ircd.limits.chanlen,
                        CHANLEN);
                ircd.limits.chanlen = CHANLEN;
            } else if (ircd.limits.chanlen <= 9)  {
                log_warn("you cannot set chanlen < 9.");
                log_warn("setting chanlen from %d to %d", ircd.limits.chanlen,
                        CHANLEN);
                ircd.limits.chanlen = CHANLEN;
            }
        }
    }

    stmp = conf_find_entry("info", conf, 1);
    if (stmp != NULL)
        strncpy(ircd.me->info, stmp, GCOSLEN);
    else
        strncpy(ircd.me->info, "no info provided.", GCOSLEN);

    stmp = conf_find_entry("network", conf, 1);
    if (stmp != NULL)
        strncpy(ircd.network, stmp, GCOSLEN);
    else
        strncpy(ircd.network, "the-one-armed-network", GCOSLEN);

    stmp = conf_find_entry("version", conf, 1);
    if (stmp != NULL)
        snprintf(ircd.version, GCOSLEN, stmp, me.version, ircd.realversion,
                me.revision);

    stmp = conf_find_entry("version-comment", conf, 1);
    if (stmp != NULL)
        snprintf(ircd.vercomment, TOPICLEN, stmp, me.version, ircd.realversion,
                me.revision);

    if (str_conv_bool(conf_find_entry("hub", conf, 1), 0))
        ircd.me->flags |= IRCD_SERVER_HUB;

    /* now admin info, yuck */
    ctmp = conf_find_list("admin", conf, 1);
    if (ctmp != NULL) {
        *ircd.admininfo.line1 = *ircd.admininfo.line2 = *ircd.admininfo.line3 = '\0';
        stmp = conf_find_entry("", ctmp, 1);
        if (stmp != NULL) {
            strncpy(ircd.admininfo.line1, stmp, GCOSLEN);
            stmp = conf_find_entry_next("", stmp, ctmp, 1);
            if (stmp != NULL) {
                strncpy(ircd.admininfo.line2, stmp, GCOSLEN);
                stmp = conf_find_entry_next("", stmp, ctmp, 1);
                if (stmp != NULL)
                    strncpy(ircd.admininfo.line3, stmp, GCOSLEN);
            }
        }
    }
        
    /* do this after the above.. */
    stmp = conf_find_entry("network-full", conf, 1);
    if (stmp != NULL)
        strncpy(ircd.network_full, stmp, TOPICLEN);
    else
        strncpy(ircd.network_full, ircd.network, TOPICLEN);

    ircd_parse_ports(conf);
}

static void ircd_parse_ports(conf_list_t *conf) {
    struct isocket_list oldlist;
    isocket_t *isp;
    int pstart, pend;
    char *s, *s2;
    char *ports;
    char *addr;
    conf_entry_t *cep = NULL;
    bool ssl;

    /* preserve the list of old listeners, we will close old listening ports if
     * they become invalidated */
    LIST_INIT(&oldlist);
    while (!LIST_EMPTY(ircd.lists.listeners)) {
        isp = LIST_FIRST(ircd.lists.listeners);
        LIST_REMOVE(isp, lp);
        LIST_INSERT_HEAD(&oldlist, isp, lp);
    }
    LIST_INIT(ircd.lists.listeners);

    /* now parse each line or section. */
    while ((cep = conf_find_next("ports", NULL, 0, cep, conf, 1)) != NULL) {
        ssl = false;
        if (cep->type == CONF_TYPE_DATA) {
            addr = ircd.address;
            ports = cep->string;
        } else {
            ssl = str_conv_bool(conf_find_entry("ssl", cep->list, 1), false);
            addr = conf_find_entry("address", cep->list, 1);
            if (addr == NULL)
                addr = ircd.address;
            ports = conf_find_entry("ports", cep->list, 1);
            if (ports == NULL) {
                log_warn("bogus ports section in ircd conf.");
                continue;
            }
        }


        /* now, with that out of way, the actual code goes here...we try and
         * parse through a string of format port1,port2,port3-port6,...,
         * adding ports as they come... */
        s = s2 = ports;
        while (*s) {
            pstart = strtol(s, &s2, 10);
            if (s == s2) {
                s++;
                continue;
            }
            s = s2;

            if (*s == '-') {
                int i;
                s++;
                pend = strtol(s, &s2, 10);
                if (s == s2) {
                    s++;
                    continue;
                }
                s = s2;
                if (pend > pstart && pstart != 0 && pend != 0) {
                    for (i = pstart; i <= pend; i++) {
                        ircd_add_listener(&oldlist, addr, i, ssl);
                    }
                }
            } else
                ircd_add_listener(&oldlist, addr, pstart, ssl);

            /* we may be at the end of the string now ... */
            if (*s == '\0')
                break;

            s++;
        }
    }

    /* last but not least, wipe out our old ports that are now dead */
    if (!LIST_EMPTY(&oldlist)) {
        isocket_t *sp1, *sp2;
        sp1 = LIST_FIRST(&oldlist);
        while (sp1 != NULL) {
            sp2 = LIST_NEXT(sp1, lp);
            destroy_socket(sp1);
            sp1 = sp2;
        }
    }
}

/* this function is called above to add a listening socket on the given
 * address/port.  it needs the old list of listening sockets, the address, and
 * the port. */
static void ircd_add_listener(struct isocket_list *oldlist, char *addr,
        unsigned short port, bool ssl) {
    isocket_t *isp;
    char sport[NI_MAXSERV];
    int gsa_port;
    char gsa_addr[NI_MAXHOST];

    log_debug("adding listener on %s/%d%s", addr, port,
            (ssl == true ? " (ssl)" : ""));

    /* See if there's already a listening socket opened. */
    LIST_FOREACH(isp, oldlist, lp) {
        get_socket_address(isock_laddr(isp), gsa_addr, NI_MAXHOST, &gsa_port);
        if (gsa_port == port && !strcasecmp(gsa_addr, addr)) {
            LIST_REMOVE(isp, lp);
            LIST_INSERT_HEAD(ircd.lists.listeners, isp, lp);
            return;
        }
    }

    sprintf(sport, "%d", port);
    isp = create_socket();
    if (isp != NULL) {
        set_socket_address(isock_laddr(isp), addr, sport, SOCK_STREAM);
        if (open_socket(isp)) {
            if (!socket_listen(isp))
                destroy_socket(isp);
            else {
#ifdef HAVE_OPENSSL
                if (ssl && !socket_ssl_enable(isp)) {
                    log_warn("could not enable SSL on %s/%d", addr, port);
                    destroy_socket(isp);
                    return;
                }
#endif
                add_hook(isp->datahook, ircd_listen_hook);
                socket_monitor(isp, SOCKET_FL_READ);
                LIST_INSERT_HEAD(ircd.lists.listeners, isp, lp);
            }
        } else
            destroy_socket(isp);
    }
}

static void ircd_parse_protocols(conf_list_t *conf) {
    char *ctmp = NULL;

    while ((ctmp = conf_find_entry_next("", ctmp, conf, 1)) != NULL) {
        /* see if the protocol has already been loaded, if so, move along */
        if (find_protocol(ctmp) != NULL)
            continue;

        if (add_protocol(ctmp))
            log_debug("added protocol support for %s", ctmp);
    }
}

static void ircd_parse_commands(conf_list_t *conf) {
    conf_entry_t *ctmp = NULL;
    char mname[PATH_MAX];

    while ((ctmp = conf_find_next("command", NULL, 0, ctmp, conf, 1))
            != NULL) {
        if (ctmp->string == NULL) {
            /* unnamed, pass */
            log_warn("attempt to add an unnamed command ignored");
            continue;
        }
        if (find_command(ctmp->string) == NULL) {
            sprintf(mname, "ircd/commands/%s", ctmp->string);
            if (load_module(mname, MODULE_FL_CREATE|MODULE_FL_QUIET))
                log_debug("loaded command module for %s", ctmp->string);
        } else        /* call 'add_command' to update the conf stuff, anyhow. */
            add_command(ctmp->string);
    }
}

static void ircd_parse_classes(conf_list_t *conf) {
    conf_list_t *c;
    conf_entry_t *cep;
    class_t *cls, *cls2;
    class_t *dcls;
    char *s;

    /* mark all of our classes dead, additionally drop any with 0 clients */
    cls = LIST_FIRST(ircd.lists.classes);
    while (cls != NULL) {
        cls2 = LIST_NEXT(cls, lp);
        cls->dead = 1;
        destroy_class(cls); /* destroy it, maybe */
        cls = cls2;
    }

    cep = NULL;
    while ((cep = conf_find_next("class", NULL, CONF_TYPE_LIST, cep, conf, 1))
            != NULL) {
        if (cep->list == NULL) {
            log_warn("class entry %s incorrectly specified", cep->string);
            continue;
        }
        c = cep->list;
        if ((s = conf_find_entry("name", c, 1)) == NULL)
            s = cep->string;
        if (s == NULL) {
            log_warn("class entry with no name, ignored.");
            continue; /* no name.  fuh. */
        }

        dcls = LIST_FIRST(ircd.lists.classes);

        if ((cls = find_class(s)) != NULL) {
            cls->dead = 0;
            free(cls->default_mode);
        } else
            cls = create_class(s);

        cls->freq = str_conv_time(conf_find_entry("ping", c, 1), 180);
        cls->max = str_conv_int(conf_find_entry("max", c, 1), 600);
        cls->flood = str_conv_int(conf_find_entry("flood", c, 1), 192);
        cls->sendq = str_conv_int(conf_find_entry("sendq", c, 1), 1000);

        if ((s = conf_find_entry("message-set", c, 1)) != NULL)
            cls->mset = find_message_set(s);
        if (cls->mset == NULL)
            cls->mset = (dcls != NULL ? dcls->mset :
                    LIST_FIRST(ircd.messages.sets));
        if ((s = conf_find_entry("privilege-set", c, 1)) != NULL)
            cls->pset = find_privilege_set(s);
        if (cls->pset == NULL)
            cls->pset = (dcls != NULL ? dcls->pset :
                    LIST_FIRST(ircd.privileges.sets));

        if ((s = conf_find_entry("mode", c, 1)) != NULL)
            cls->default_mode = strdup(s);
        else
            cls->default_mode = strdup((dcls != NULL ?
                        dcls->default_mode : "+i"));

        cls->conf = c;
    }
}

static void ircd_parse_message_sets(conf_list_t *conf) {
    conf_entry_t *cp = NULL;

    while ((cp = conf_find_next("message-set", NULL, CONF_TYPE_LIST, cp, conf,
                    1)) != NULL) {
        if (cp->string != NULL && *cp->string != '\0')
            create_message_set(cp->string, cp->list);
    }
}

static void ircd_parse_privilege_sets(conf_list_t *conf) {
    conf_entry_t *cp = NULL;
    privilege_set_t *psp = NULL;

    ircd.privileges.oper_set = NULL;
    while ((cp = conf_find_next("privilege-set", NULL, CONF_TYPE_LIST, cp,
                    conf, 1)) != NULL) {
        if (cp->string != NULL && *cp->string != '\0') {
            psp = create_privilege_set(cp->string, cp->list);
            if (ircd.privileges.oper_set == NULL &&
                    *(int64_t *)psp->vals[ircd.privileges.priv_operator] !=
                    false)
                ircd.privileges.oper_set = psp;
        }
    }
}

static void ircd_parse_addons(conf_list_t *conf) {
    char *s = NULL;
    char mname[PATH_MAX];

    while ((s = conf_find_entry_next("addon", s, conf, 1)) != NULL) {
        sprintf(mname, "ircd/addons/%s", s);

        if (module_loaded(mname) != 0)
            continue;

        if (!load_module(mname,
                    MODULE_FL_CREATE|MODULE_FL_QUIET|MODULE_FL_EXPORT))
            log_error("unable to load module for addon %s", s);
        else
            log_debug("loaded addon module %s", s);
    }
}

/* this simply makes sure that the conf pointers for all linked servers are
 * up-to-date (useful when you /rehash and your configuration lists are shot
 * right to hell) */
static void ircd_parse_servers(conf_list_t *conf) {
    server_t *srv;
    conf_entry_t *cep;
    char *s;
    struct server_connect *scp;

    /* do the re-association */
    LIST_FOREACH(srv, ircd.lists.servers, lp) {
        cep = conf_find("server", srv->name, CONF_TYPE_LIST, conf, 1);
        if (cep != NULL) {
            srv->conf = cep->list;
            /* update their flags, too. */
            server_set_flags(srv);
        } else
            srv->conf = NULL; /* important to re-set this if we need to */
    }

    /* Wipe out any existing auto-connect info.. */
    while (!LIST_EMPTY(ircd.lists.server_connects))
        destroy_server_connect(LIST_FIRST(ircd.lists.server_connects));

    /* And add new ones */
    cep = NULL;
    while ((cep = conf_find_next("server", NULL, CONF_TYPE_LIST, cep, conf, 1))
            != NULL) {
        if (cep->string == NULL)
            continue;
        
        /* see if we can create a 'server_connect' entry for this server.
         * basically, if it has an address and default port then we can. */
        if ((s = conf_find_entry("address", cep->list, 1)) != NULL &&
                (s = conf_find_entry("port", cep->list, 1)) != NULL) {

            scp = find_server_connect(cep->string);
            if (scp == NULL) {
                /* create a new one */
                scp = create_server_connect(cep->string);
                scp->conf = cep->list;
                scp->last = me.now;
            } else {
                scp->conf = cep->list;
                scp->last = me.now;
            }
            scp->interval = 0;
            if ((s = conf_find_entry("interval", cep->list, 1)) != NULL)
                scp->interval = str_conv_time(s, 0);
        }
    }
}

/******************************************************************************
  * hostlist stuff below here ..
  ****************************************************************************/
hostlist_t *create_host_list(char *name) {
    hostlist_t *hlp = find_host_list(name);

    if (hlp != NULL)
        destroy_host_list(hlp);
    hlp = malloc(sizeof(hostlist_t));
    hlp->name = strdup(name);
    hlp->entries = 0;
    hlp->list = NULL;

    LIST_INSERT_HEAD(ircd.lists.hostlists, hlp, lp);

    return hlp;
}

hostlist_t *find_host_list(char *name) {
    hostlist_t *hlp;

    LIST_FOREACH(hlp, ircd.lists.hostlists, lp) {
        if (!strcasecmp(hlp->name, name))
            return hlp;
    }

    return NULL;
}

void destroy_host_list(hostlist_t *hlp) {
    int i;

    free(hlp->name);
    for (i = 0;i < hlp->entries;i++)
        free(hlp->list[i]);
    if (hlp->list != NULL)
        free(hlp->list);
    LIST_REMOVE(hlp, lp);
    free(hlp);
}

void add_to_host_list(hostlist_t *hlp, char *val) {

    hlp->list = realloc(hlp->list, sizeof(char *) * (hlp->entries + 1));
    hlp->list[hlp->entries++] = strdup(val);
}

void del_from_host_list(hostlist_t *hlp, char *val) {
    int i;
    char **newlist = NULL;
    int newsize = 0;

    for (i = 0;i < hlp->entries;i++) {
        if (strcasecmp(hlp->list[i], val)) {
            /* add it if it doesn't compare.. */
            newlist = realloc(newlist, sizeof(char *) * (newsize + 1));
            newlist[newsize++] = hlp->list[i];
        }
    }
    free(hlp->list);
    hlp->list = newlist;
    hlp->entries = newsize;
}

static void ircd_parse_host_lists(conf_list_t *conf) {
    hostlist_t *hlp;
    conf_entry_t *cep = NULL;
    char *s;

    while ((cep = conf_find_next("host-list", NULL, CONF_TYPE_LIST, cep, conf,
                    1)) != NULL) {
        if (cep->string == NULL) {
            log_warn("host-list with no name!");
            continue;
        }

        hlp = create_host_list(cep->string);
        s = NULL;
        while ((s = conf_find_entry_next("", s, cep->list, 1)) != NULL)
            add_to_host_list(hlp, s);
    }
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
