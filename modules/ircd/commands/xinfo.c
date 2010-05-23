/*
 * xinfo.c: the XINFO (eXtended INFO) command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: xinfo.c 780 2006-10-02 01:30:16Z wd $");

MODULE_REGISTER("$Rev: 780 $");
/*
@DEPENDENCIES@: ircd
*/

static XINFO_FUNC(xinfo_class_handler);
static XINFO_FUNC(xinfo_client_handler);
static XINFO_FUNC(xinfo_connects_handler);
static XINFO_FUNC(xinfo_hash_handler);
static XINFO_FUNC(xinfo_me_handler);
static XINFO_FUNC(xinfo_privilege_handler);
static XINFO_FUNC(xinfo_server_handler);
static XINFO_FUNC(xinfo_xinfo_handler);

static void xinfo_connection_data(client_t *, connection_t *);

MODULE_LOADER(xinfo) {

    add_xinfo_handler(xinfo_class_handler, "CLASS", 0,
            "Provides information about server connection classes");
    add_xinfo_handler(xinfo_client_handler, "CLIENT", XINFO_HANDLER_OPER,
            "Provices information about clients on this server");
    add_xinfo_handler(xinfo_connects_handler, "CONNECTS", XINFO_HANDLER_OPER,
            "Shows information about server uplinks");
    add_xinfo_handler(xinfo_hash_handler, "HASH", XINFO_HANDLER_OPER,
            "Shows hash table statistics.");
    add_xinfo_handler(xinfo_me_handler, "ME", XINFO_HANDLER_LOCAL,
            "Provides information about your connection statistics");
    add_xinfo_handler(xinfo_privilege_handler, "PRIVILEGE",
            XINFO_HANDLER_LOCAL | XINFO_HANDLER_OPER,
            "Provides information about available privileges");
    add_xinfo_handler(xinfo_server_handler, "SERVER", 0,
            "Provides information about this (or other) servers");
    add_xinfo_handler(xinfo_xinfo_handler, "XINFO", 0,
            "Provides a list of available XINFO query-handlers");

    /* now create numerics */
#define ERR_AMBIGUOUSXINFO 770
    CMSG("770", "%s :Ambiguous handler name");
#define ERR_NOSUCHXINFO 772
    CMSG("772", "%s :No such handler");
#define RPL_XINFOSTART 773
    CMSG("773", "%s :extended info reply");
#define RPL_XINFOEND 774
    CMSG("774", "%s :end of extended info");

    return 1;
}
MODULE_UNLOADER(xinfo) {

    remove_xinfo_handler(xinfo_class_handler);
    remove_xinfo_handler(xinfo_client_handler);
    remove_xinfo_handler(xinfo_me_handler);
    remove_xinfo_handler(xinfo_privilege_handler);
    remove_xinfo_handler(xinfo_server_handler);
    remove_xinfo_handler(xinfo_xinfo_handler);

    DMSG(ERR_AMBIGUOUSXINFO);
    DMSG(ERR_NOSUCHXINFO);
    DMSG(RPL_XINFOSTART);
    DMSG(RPL_XINFOEND);
}

/* the XINFO command:  General format is:
 * XINFO [server.name.or.mask] <query> [args]
 * only one query may be performed at a time.  different subsystems may install
 * different 'xinfo handlers' so there may be many available handlers, or very
 * few. */
CLIENT_COMMAND(xinfo, 0, 3, COMMAND_FL_FOLDMAX) {
    int harg = 0; /* the argument containing our handler name */
    struct xinfo_handler *xhp;
    int len;

    if (argc > 1 && strchr(argv[1], '.') != NULL) {
        if (argc < 3)
            return COMMAND_WEIGHT_NONE; /* don't waste our time */
        /* if the argument appears to be a servername, see about sending it
         * along. */
        if (pass_command(cli, NULL, "XINFO", (argc > 3 ? "%s %s %s" : "%s %s"),
                    argc, argv, 1) != COMMAND_PASS_LOCAL)
            return COMMAND_WEIGHT_EXTREME;
        harg = 2; /* argv[2] is the handler argument. */
    } else if (argc > 1) {
        /* not sent to the server.  we might need to fold stuff down from
         * argv[3] into argv[2]. */
        if (argc > 3) {
            strlcat(argv[2], " ", COMMAND_MAXARGLEN);
            strlcat(argv[2], argv[3], COMMAND_MAXARGLEN);
        }
        harg = 1;
    }

    /* find the handler, if any */
    if (harg == 0)
        xhp = find_xinfo_handler("XINFO");
    else {
        /* we don't use find_xinfo_handler because we want to allow partial
         * non-ambiguous names for convenience. */
        len = strlen(argv[harg]);
        LIST_FOREACH(xhp, ircd.lists.xinfo_handlers, lp) {
            if (!strncasecmp(argv[harg], xhp->name, len)) {
                /* if the next entry doesn't match as well then this will be
                 * non-ambiguous since the handler list is sorted */
                if (LIST_NEXT(xhp, lp) != NULL &&
                        !strncasecmp(argv[harg], LIST_NEXT(xhp, lp)->name,
                            len)) {
                    sendto_one(cli, RPL_FMT(cli, ERR_AMBIGUOUSXINFO),
                            argv[harg]);
                    return COMMAND_WEIGHT_LOW;
                }
                break;
            }
        }
        
        if (xhp == NULL) {
            sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHXINFO), argv[harg]);
            return COMMAND_WEIGHT_LOW;
        }
    }

    /* got a handler, now see if they can use it. */
    if (xhp->flags & XINFO_HANDLER_LOCAL && !MYCLIENT(cli))
        return COMMAND_WEIGHT_LOW; /* silently ignore these */
    if ((xhp->flags & XINFO_HANDLER_OPER && !OPER(cli)) ||
            !BPRIV(cli, xhp->priv)) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOPRIVILEGES));
        return COMMAND_WEIGHT_LOW;
    }

    sendto_one(cli, RPL_FMT(cli, RPL_XINFOSTART), xhp->name);
    xhp->func(cli, argc - harg, argv + harg);
    sendto_one(cli, RPL_FMT(cli, RPL_XINFOEND), xhp->name);

    return COMMAND_WEIGHT_HIGH;
}

static XINFO_FUNC(xinfo_class_handler) {
    class_t *cls = NULL;
    char rpl[XINFO_LEN];

    if (argc > 1) {
        cls = find_class(argv[1]);
        if (cls != NULL) {
            snprintf(rpl, XINFO_LEN,
                    "NAME %s PINGFREQ %d MAX %d SENDQ %d FLOOD %d CLIENTS %d",
                    cls->name, cls->freq, cls->max, cls->sendq, cls->flood,
                    cls->clients);
            sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "DATA", rpl);
            sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "MODE",
                    cls->default_mode);
            sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "MSET",
                    cls->mset->name);
            sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "PSET",
                    cls->pset->name);
        } else {
            snprintf(rpl, XINFO_LEN, "nonexistant class %s", argv[1]);
            sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "ERROR", rpl);
        }
    } else {
        LIST_FOREACH(cls, ircd.lists.classes, lp) {
            snprintf(rpl, XINFO_LEN, "NAME %s PINGFREQ %d MAX %d SENDQ %d",
                    cls->name, cls->freq, cls->max, cls->sendq);
            sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "DATA", rpl);
        }
    }
}

static XINFO_FUNC(xinfo_client_handler) {
    client_t *cp;
    char rpl[XINFO_LEN];
    char *user;
    char ip[IPADDR_MAXLEN + 1];

    if (argc < 2) {
        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "ERROR",
                "You must specify a nickname");
        return;
    } else if ((cp = find_client(argv[1])) == NULL) {
        snprintf(rpl, XINFO_LEN, "%s does not exist", argv[1]);
        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "ERROR", rpl);
        return;
    } else if (!MYCLIENT(cp)) {
        snprintf(rpl, XINFO_LEN, "%s is not a local client", cp->nick);
        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "ERROR", rpl);
        return;
    }
                
    if (cp->conn != NULL) {
        /* give them the connection info if we can... */
        if (*cp->conn->user == '\0' || *cp->conn->user == '~')
            user = cp->user; /* this information is not valuable here */
        else
            user = cp->conn->user;
        if (cp->conn->sock != NULL) /* should never be NULL but.. */
            get_socket_address(isock_raddr(cp->conn->sock), ip,
                    IPADDR_MAXLEN + 1, NULL);
        else
            strcpy(ip, cp->ip);
        snprintf(rpl, XINFO_LEN, "%s!%s@%s[%s]", cp->nick, user,
                cp->conn->host, ip);
    } else
        snprintf(rpl, XINFO_LEN, "%s!%s@%s[%s]", cp->nick, cp->user,
                cp->host, cp->ip);
    sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "ADDRESS", rpl);
    snprintf(rpl, XINFO_LEN, "TS %d SIGNON %d IDLE %d", cp->ts,
            cp->signon, me.now - cp->last);
    sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "TIMES", rpl);
    if (cp->conn != NULL)
        xinfo_connection_data(cli, cp->conn);
    sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "PSET", cp->pset->name);
}

static XINFO_FUNC(xinfo_connects_handler) {
    struct server_connect *scp;
    char rpl[XINFO_LEN];
    bool hidden;

    LIST_FOREACH(scp, ircd.lists.server_connects, lp) {
        if (argc > 1 && !match(argv[1], scp->name))
            continue; /* skip unwanted ones */
        hidden = str_conv_bool(conf_find_entry("hidden", scp->conf, 1), false);
        if (!hidden || (hidden && BPRIV(cli, ircd.privileges.priv_shs))) {
            char *addr, *port, *cls, *proto, *ssl, *hub;

            addr = conf_find_entry("address", scp->conf, 1);
            proto = conf_find_entry("protocol", scp->conf, 1);
            if ((port = conf_find_entry("port", scp->conf, 1)) == NULL)
                port = "6667";
            if ((cls = conf_find_entry("class", scp->conf, 1)) == NULL)
                cls = ((class_t *)LIST_FIRST(ircd.lists.classes))->name;
            ssl = bool_conv_str(str_conv_bool(conf_find_entry("ssl", scp->conf,
                            1), false), "yes", "no");
            if (addr != NULL && proto != NULL) {
                sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "SERVER", scp->name);
                snprintf(rpl, XINFO_LEN, "ADDRESS %s PORT %s CLASS %s "
                        "PROTOCOL %s SSL %s", addr, port, cls, proto, ssl);
                sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "CONNECT", rpl);
            }

            if (argc > 1 || (addr == NULL || proto == NULL)) {
                /* if they asked about a specific server send extra data about
                 * it */
                if (addr == NULL || proto == NULL)
                    sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "SERVER",
                            scp->name);
                snprintf(rpl, XINFO_LEN, "INTERVAL %s LAST %d HIDDEN %s "
                        "MASTER %s", time_conv_str(scp->interval), scp->last,
                        bool_conv_str(hidden, "yes", "no"),
                        bool_conv_str(str_conv_bool(conf_find_entry("master",
                                    scp->conf, 1), false), "yes", "no"));
                sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "CONFIG", rpl);

                hub = NULL;
                while ((hub = conf_find_entry_next("hub", hub, scp->conf, 1))
                        != NULL)
                    sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "HUB", hub);
            }
        }
    }
}

static XINFO_FUNC(xinfo_hash_handler) {
    char rpl[XINFO_LEN];

#ifdef DEBUG_CODE
# define XINFO_SHOW_HASH(_hash, _name) do {                                \
    snprintf(rpl, XINFO_LEN, "HASH " _name " SIZE %d ENTRIES %d "        \
            "MPB %d EMPTY %d", hashtable_size(_hash),                        \
            hashtable_count(_hash), _hash->max_per_bucket,                \
            _hash->empty_buckets);                                        \
    sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "HASHINFO", rpl);                \
} while (0)
#else
# define XINFO_SHOW_HASH(_hash, _name) do {                                \
    snprintf(rpl, XINFO_LEN, "HASH " _name " SIZE %d ENTRIES %d",        \
            hashtable_size(_hash), hashtable_count(_hash));                \
    sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "HASHINFO", rpl);                \
} while (0)
#endif

    /* send the hash info for the hashes we know about.. */
    XINFO_SHOW_HASH(ircd.hashes.client, "clients");
    XINFO_SHOW_HASH(ircd.hashes.client_history, "client_history");
    XINFO_SHOW_HASH(ircd.hashes.command, "commands");
    XINFO_SHOW_HASH(ircd.hashes.channel, "channels");
}

static XINFO_FUNC(xinfo_me_handler) {
    /* this is just a wrapper to xinfo_client_handler, but without the
     * associated privilege check.  mock up a fake argv and all that. */
    char *fakeargv[2];

    fakeargv[0] = "CLIENT";
    fakeargv[1] = cli->nick;
    xinfo_client_handler(cli, 2, fakeargv);
}

static XINFO_FUNC(xinfo_privilege_handler) {
    char rpl[XINFO_LEN];
    int i;
    struct privilege_set *psp;
    privilege_t *pp;
    client_t *fakecli = NULL;

    LIST_FOREACH(psp, ircd.privileges.sets, lp)
        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "SET", psp->name);

    for (i = 0;i < ircd.privileges.size;i++) {
        if (ircd.privileges.privs[i].num != i)
            continue; /* empty */
        if (!strncmp(ircd.privileges.privs[i].name, "command-", 8))
            continue; /* skip auto-generated command privileges. */
        pp = ircd.privileges.privs + i;
        snprintf(rpl, XINFO_LEN, "NAME %s TYPE ", pp->name);
        if (pp->flags & PRIVILEGE_FL_BOOL) {
            strlcat(rpl, "boolean DEFAULT ", XINFO_LEN);
            strlcat(rpl, bool_conv_str(BPRIV(fakecli, i), "on/yes", "off/no"),
                    XINFO_LEN);
        } else if (pp->flags & PRIVILEGE_FL_INT) {
            strlcat(rpl, "integer DEFAULT ", XINFO_LEN);
            strlcat(rpl, int_conv_str(IPRIV(fakecli, i)), XINFO_LEN);
        } else if (pp->flags & PRIVILEGE_FL_STR) {
            strlcat(rpl, "string DEFAULT ", XINFO_LEN);
            strlcat(rpl, SPRIV(fakecli, i), XINFO_LEN);
        } else if (pp->flags & PRIVILEGE_FL_TUPLE) {
            /* YUCK! */
            struct privilege_tuple *ptp;
            int64_t def = TPRIV(fakecli, i);
            int j;

            strlcat(rpl, "tuple DEFAULT ", XINFO_LEN);
            for (j = 0, ptp = pp->tuples;ptp[j].name != NULL;j++) {
                if (ptp[j].val == def)
                    break;
            }
            if (ptp[j].val == def)
                strlcat(rpl, ptp[j].name, XINFO_LEN);
            else
                strlcat(rpl, int_conv_str(def), XINFO_LEN);
        } else
            strlcat(rpl, "unknown", XINFO_LEN);

        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "PRIVILEGE", rpl);
    }
}

/* this one is pretty complicated.  if there is no argument it provides a bevy
 * of information on this server.  if there is an argument it provides
 * information on what the server thinks about the other server (whether it is
 * currently linked, where it comes from, etc etc). */
static XINFO_FUNC(xinfo_server_handler) {
    char rpl[XINFO_LEN];
    server_t *sp = NULL;
    struct server_connect *scp = NULL;
    conf_list_t *clp = NULL;
    int sent = 0; /* set this if we send *something*.  if we don't, error. */

    if (argc < 2)
        sp = ircd.me;
    else {
        conf_entry_t *cep;

        sp = find_server(argv[1]);
        scp = find_server_connect(argv[1]);
        if ((cep = conf_find("server", argv[1], CONF_TYPE_LIST,
                        *ircd.confhead, 1)) != NULL)
            clp = cep->list;
    }

    sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "NAME",
            (sp != NULL ? sp->name : argv[1]));

    if (sp != NULL && CAN_SEE_SERVER(cli, sp)) {
        sent++;

        snprintf(rpl, XINFO_LEN, "HOPS %d", sp->hops);
        if (sp->parent != NULL) {
            strlcat(rpl, " FROM ", XINFO_LEN);
            strlcat(rpl, sp->parent->name, XINFO_LEN);
        }
        strlcat(rpl, " INFO ", XINFO_LEN);
        strlcat(rpl, sp->info, XINFO_LEN);
        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "INFO", rpl);
        if (sp == ircd.me) {
            /* send them extra stuff all about us */
            sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "UPTIME", 
                    time_conv_str(me.now - me.started));
        } else if (sp->conn != NULL && OPER(cli)) {
            sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "CONNECTED",
                    int_conv_str(sp->conn->signon));
            xinfo_connection_data(cli, sp->conn);
        }
    }

    /* I'm restricting info about server connects to operators.  I'm not sure
     * what else would be amicable. */
    if (OPER(cli) && scp != NULL) {
        sent++;
        snprintf(rpl, XINFO_LEN, "ADDRESS %s PORT %s LAST %d INTERVAL %s",
                conf_find_entry("address", scp->conf, 1),
                conf_find_entry("port", scp->conf, 1),
                scp->last, time_conv_str(scp->interval));
        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "CONNECT", rpl);
    }

    /* last, but not least, report on interesting configuration bits about the
     * server, if it has interesting configuration bits. */
    if (OPER(cli) && clp != NULL) {
        char *s;
        if (sp == NULL && (s = conf_find_entry("protocol", clp, 1)) != NULL)
            sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "PROTOCOL", s);
        s = NULL;
        while ((s = conf_find_entry_next("hub", s, clp, 1)) != NULL)
            sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "HUB", s);
    }

    if (!sent)
        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "ERROR",
                "Nothing known about that server");
}

static XINFO_FUNC(xinfo_xinfo_handler) {
    struct xinfo_handler *xhp;

    LIST_FOREACH(xhp, ircd.lists.xinfo_handlers, lp) {
        /* only show them the handler if they can access it.. */
        if (xhp->flags & XINFO_HANDLER_LOCAL && !MYCLIENT(cli))
            continue; /* non-local */
        if ((xhp->flags & XINFO_HANDLER_OPER && !OPER(cli)) ||
                !BPRIV(cli, xhp->priv))
            continue; /* not privileged */

        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), xhp->name, xhp->desc);
    }
}

/* this shows information about a connection.  it assumes it is being called
 * from some other handler. */
static void xinfo_connection_data(client_t *cli, connection_t *conn) {
    char rpl[XINFO_LEN];

    snprintf(rpl, XINFO_LEN, "SENT %lld PSENT %lld RECV %lld PRECV %lld",
            conn->stats.sent, conn->stats.psent,
            conn->stats.recv, conn->stats.precv);
    sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "CONNSTAT", rpl);
    sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "CLASS", conn->cls->name);
    if (conn->mset != NULL)
        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "MSET", conn->mset->name);
    sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "PROTOCOL", conn->proto->name);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
