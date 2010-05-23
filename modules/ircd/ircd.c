/*
 * ircd.c: the main() type area for the ircd module
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains the module load/unload functions, as well as some of the
 * more major hooks that aren't in socket.c
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "../ident/ident.h"
#include "../dns/dns.h"
#include "../dns/lookup.h"

IDSTRING(rcsid, "$Id: ircd.c 818 2008-09-21 22:00:54Z wd $");

MODULE_REGISTER("1.0r0");
/*
@DEPENDENCIES@: dns ident
*/

HOOK_FUNCTION(ircd_reload_hook);
HOOK_FUNCTION(ircd_loadmodule_hook);

/* this handles counting the default modes for stats, and ensuring +o is
 * maintained correctly. */
USERMODE_FUNC(mode_set_counter);

struct ircd_struct ircd;
union cptr_u cptr;
server_t *sptr;

/* the 'timer fuzz' is the fuzziness of the ircd timer hook.  this is the
 * number of seconds between each execution of the timer hook.  the reason you
 * might not want to run the timer hook at every pass (or second) is because it
 * mostly performs timeouts and other reaping of resources, and on a small
 * server the usage isn't as critical.  on large servers every second can mean
 * tens or even hundreds of timeouts and resource regains. */
#define TIMER_FUZZ_LOW 15
#define TIMER_FUZZ_LOWCNT 512
#define TIMER_FUZZ_MED 10
#define TIMER_FUZZ_MEDCNT 2048
#define TIMER_FUZZ_HIGH 5
static time_t timer_fuzz = TIMER_FUZZ_LOW;
static timer_ref_t timer_ref;

HOOK_FUNCTION(ircd_timer_hook) {
    time_t now = me.now;
    connection_t *cp, *cp2;
    struct server_connect *scp;
    time_t fuzz;

    /* see if we want to adjust the timer fuzziness this go-round */
    if (ircd.stats.serv.curclients <= TIMER_FUZZ_LOWCNT)
        fuzz = TIMER_FUZZ_LOW;
    else if (ircd.stats.serv.curclients <= TIMER_FUZZ_MEDCNT)
        fuzz = TIMER_FUZZ_MED;
    else
        fuzz = TIMER_FUZZ_HIGH;
    if (timer_fuzz != fuzz) {
        timer_fuzz = fuzz;
        adjust_timer(timer_ref, -1, timer_fuzz);
    }

    /* time out dead clients in stage2/clients based on their class */
    cp = LIST_FIRST(ircd.connections.stage2);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        /* for stage2, drop connections 4x faster than normal, since
         * connections should *not* sit in a stage2 situation */
        if ((now - cp->last) > (cp->cls->freq / 4))
            destroy_connection(cp, "Ping timeout");
        cp = cp2;
    }

    cp = LIST_FIRST(ircd.connections.clients);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        if ((now - cp->last) > cp->cls->freq)
            destroy_connection(cp, "Ping timeout");
        /* if 1/2 of the pingfreq time has elapsed, drop them a PING */
        else if ((now - cp->last) > (int)((float)cp->cls->freq * 0.5) &&
                !CONN_PINGSENT(cp)) {
            /* use sendto_one_target here so that there is no prefix added,
             * apparently most clients balk at the prefix */
            sendto_one_target(cp->cli, NULL, NULL, NULL, "PING", ":%s",
                    ircd.me->name);
            cp->flags |= IRCD_CONNFL_PINGSENT;
        }
        cp = cp2;
    }

    cp = LIST_FIRST(ircd.connections.servers);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        /* disconnect them. */
        if ((now - cp->last) > cp->cls->freq)
            destroy_connection(cp, "Ping timeout");
        /* if 1/2 of the pingfreq time have elapsed, drop them a PING */
        else if ((now - cp->last) > (int)((float)cp->cls->freq * 0.5) &&
                !CONN_PINGSENT(cp)) {
            /* use sendto_one_from here so that there is no prefix added,
             * apparently most clients balk at the prefix */
            sendto_serv_from(cp->srv, NULL, NULL, NULL, "PING", ":%s",
                    ircd.me->name);
            cp->flags |= IRCD_CONNFL_PINGSENT;
        }
        cp = cp2;
    }

    /* now check for autoconnects */
    LIST_FOREACH(scp, ircd.lists.server_connects, lp) {
        /* if a connection isn't in progress and it has been long enough since
         * we last connected and the server isn't already on the network, go
         * ahead and try */
        if (scp->srv == NULL && scp->interval > 0 &&
                scp->last + scp->interval < me.now &&
                find_server(scp->name) == NULL)
            server_connect(scp, NULL);
    }

    return NULL;
}

HOOK_FUNCTION(ircd_reload_hook) {

    /* if the configuration parser fails, we have a bit of a nasty problem.
     * we hope, mostly, that nothing is broken by doing this. */
    if (!ircd_parse_conf(*ircd.confhead))
        log_warn("a problem occured while handling the configuration data for "
                "ircd.  Old configuration will be preserved.");

    /* re-allocate 'sends', (maxsockets may change across a reload */
    free(ircd.sends);
    ircd.sends = malloc(sizeof(char) * maxsockets);

    return NULL;
}

/* this function is called when modules are loaded or unloaded.  it is useful
 * to add command modules, as well as to update pointers for other modules in
 * the system. */
HOOK_FUNCTION(ircd_loadmodule_hook) {
    char *s = (char *)data;

    if (match("ircd/protocols/*", s)) {
        s = strrchr(s, '/');
        s++;
        if (ep == me.events.load_module)
            update_protocol(s);
        else if (ep == me.events.unload_module)
            remove_protocol(s);
    } else if (match("ircd/commands/*", s)) {
        s = strrchr(s, '/');
        s++;

        if (ep == me.events.load_module)
            add_command(s);
        else if (ep == me.events.unload_module)
            remove_command(s);
    }

    return NULL;
}

/* the i/o counter goodies */
USERMODE_FUNC(mode_set_counter) {
    int i;

    switch (mode) {
        case 'i':
            if (set)
                ircd.stats.net.visclients--;
            else
                ircd.stats.net.visclients++;
            break;
        case 'o':
            /* if this is our client, isn't set +o, and doesn't have the
             * privilege to do, deny. */
            if (MYCLIENT(cli) && by == cli && !OPER(cli) &&
                    !BPRIV(cli, ircd.privileges.priv_operator))
                return 0;
            if (set) {
                ircd.stats.opers++;
                if (!MYCLIENT(cli))
                    /* change them to the default operator privilege set */
                    cli->pset = ircd.privileges.oper_set;
                hook_event(ircd.events.client_oper, cli);
            } else {
                /* remove them from any operator-only send flags, also remove
                 * any operator-only modes they might have set */
                if (MYCLIENT(cli)) {
                    unsigned char *s;
                    for (s = ircd.umodes.avail;*s != '\0';s++) {
                        if (ircd.umodes.modes[*s].flags & USERMODE_FL_OPER &&
                                !(ircd.umodes.modes[*s].flags &
                                    USERMODE_FL_PRESERVE))
                            usermode_unset(*s, cli, cli, NULL, NULL);
                    }
                    for (i = 0;i < ircd.sflag.size;i++) {
                        if (ircd.sflag.flags[i].flags &
                                SEND_LEVEL_OPERATOR &&
                            !(ircd.sflag.flags[i].flags &
                                SEND_LEVEL_PRESERVE))
                            remove_from_send_flag(i, cli, true);
                    }
                }
                ircd.stats.opers--;
                hook_event(ircd.events.client_deoper, cli);
            }
            break;
    }

    return 1;
}

MODULE_LOADER(ircd) {
    conf_list_t *conf = *confdata;
    int i;
    unsigned char c;
    uint64_t ui64;
    struct tm *tmtime;
    module_t *mp = find_module("ircd"); /* grab ourselves (kinky) */
    connection_t *cp;
    isocket_t *isp;

    /* if we have no conf data this module is utterly useless.  check this
     * before we do anything else. */
    if (conf == NULL) {
        log_error("there is no configuration data for the ircd "
                "module, it will not work at all in this case.");
        return 0;
    }

    /* clear out our global data */
    memset(&ircd, 0, sizeof(ircd));

    ircd.confhead = confdata;

    /* below here we set various defaults.  we try to get module save data, if
     * possible, and if not we set various data bits instead. */
    if (!get_module_savedata(savelist, "ircd.limits", &ircd.limits)) {
        ircd.limits.nicklen = NICKLEN;
        ircd.limits.chanlen = CHANLEN;
    }

    get_module_savedata(savelist, "ircd.stats", &ircd.stats);

    /* this stuff isn't handled anywhere else below, so we get it all here
     * *first* */
    get_module_savedata(savelist, "ircd.address", ircd.address);
    get_module_savedata(savelist, "ircd.network", ircd.network);
    get_module_savedata(savelist, "ircd.network_full", ircd.network_full);
    get_module_savedata(savelist, "ircd.statsfile", ircd.statsfile);
    get_module_savedata(savelist, "ircd.started", &ircd.started);
    if (!get_module_savedata(savelist, "ircd.connections",
                &ircd.connections)) {
        LIST_ALLOC(ircd.connections.stage1);
        LIST_ALLOC(ircd.connections.stage2);
        LIST_ALLOC(ircd.connections.clients);
        LIST_ALLOC(ircd.connections.servers);
    }
    if (!get_module_savedata(savelist, "ircd.lists", &ircd.lists)) {
        LIST_ALLOC(ircd.lists.listeners);
        LIST_ALLOC(ircd.lists.servers);
        LIST_ALLOC(ircd.lists.server_connects);
        LIST_ALLOC(ircd.lists.clients);
        TAILQ_ALLOC(ircd.lists.client_history);
        LIST_ALLOC(ircd.lists.classes);
        LIST_ALLOC(ircd.lists.protocols);
        LIST_ALLOC(ircd.lists.channels);
        LIST_ALLOC(ircd.lists.commands);
        LIST_ALLOC(ircd.lists.isupport);
        LIST_ALLOC(ircd.lists.hostlists);
        LIST_ALLOC(ircd.lists.xinfo_handlers);
        LIST_ALLOC(ircd.lists.xattr);
    }

    if (!get_module_savedata(savelist, "ircd.me", &ircd.me)) {
        ircd.me = create_server(NULL);
        ircd.stats.servers++;
    }

    /* allocate our 'sends' string */
    ircd.sends = malloc(sizeof(char) * maxsockets);

    /* set our started time */
    if (!get_module_savedata(savelist, "ircd.ascstart", ircd.ascstart)) {
        tmtime = localtime(&me.started);
        strftime(ircd.ascstart, 48, "%a %b %d, %H:%M:%S %Z", tmtime);
    }

    /* version string, user-overwriteable */
    ircd.realversion = mp->header->version;
    if (!get_module_savedata(savelist, "ircd.version", ircd.version))
        snprintf(ircd.version, GCOSLEN, "%s+ircd%s.r%d", me.version,
                ircd.realversion, me.revision);
    if (!get_module_savedata(savelist, "ircd.vercomment", ircd.vercomment))
        snprintf(ircd.vercomment, TOPICLEN,
                "part of The Ithildin Project:  http://ithildin.org/");

    if (!get_module_savedata(savelist, "ircd.sflag", &ircd.sflag)) {
        ircd.sflag.ops = create_send_flag("OPERATORS",
                SEND_LEVEL_OPERATOR | SEND_LEVEL_CANTCHANGE,
                ircd.privileges.priv_operator);
        ircd.sflag.servmsg = create_send_flag("SERVMSG", 0, -1);
    }

    /* create some privileges */
    if (!get_module_savedata(savelist, "ircd.privileges", &ircd.privileges)) {
        LIST_ALLOC(ircd.privileges.sets);
        ui64 = 0;
        ircd.privileges.priv_operator = create_privilege("operator",
                PRIVILEGE_FL_BOOL, &ui64, NULL);
        ircd.privileges.priv_shs = create_privilege("see-hidden-servers",
                PRIVILEGE_FL_BOOL, &ui64, NULL);
        ircd.privileges.priv_srch = create_privilege("see-real-client-host",
                PRIVILEGE_FL_BOOL, &ui64, NULL);
    }

    /* set up our mode structures and acquire some modes */
#define MODELOOP(from, to, array) do {                                        \
    for (c = from;c <= to;c++, ui64 <<= 1) {                                  \
        ircd.array.modes[c].mode = c;                                         \
        ircd.array.modes[c].mask = ui64;                                      \
        ircd.array.modes[c].avail = 1;                                        \
    }                                                                         \
} while (0)

    if (!get_module_savedata(savelist, "ircd.umodes", &ircd.umodes)) {
        ui64 = 1;
        MODELOOP('a', 'z', umodes); /* set a-z modes */
        MODELOOP('A', 'Z', umodes); /* and A-Z modes */
        MODELOOP('0', '9', umodes); /* and 0-9 modes */

        /* we know that all modes are free.  grab i, o, and s */
        EXPORT_SYM(mode_set_counter);
        ircd.umodes.i = usermode_request('i', &c, USERMODE_FL_GLOBAL, -1,
                "mode_set_counter");
        ircd.umodes.o = usermode_request('o', &c, USERMODE_FL_GLOBAL,
                ircd.sflag.ops, "mode_set_counter");
        ircd.umodes.s = usermode_request('s', &c, 0, ircd.sflag.servmsg,
                NULL);
    }

    if (!get_module_savedata(savelist, "ircd.cmodes", &ircd.cmodes)) {
        ui64 = 1;
        MODELOOP('a', 'z', cmodes); /* do the same for channel modes */
        MODELOOP('A', 'Z', cmodes);
        MODELOOP('0', '9', cmodes);
    }

    /* setup our events.  started is always created, since it is destroyed
     * later in *this function*. :) */
    if (!get_module_savedata(savelist, "ircd.events", &ircd.events)) {
        ircd.events.stage1_connect = create_event(0);
        ircd.events.stage2_connect = create_event(0);
        ircd.events.stage3_connect = create_event(0);

        ircd.events.client_connect = create_event(EVENT_FL_NORETURN);
        ircd.events.client_disconnect = create_event(EVENT_FL_NORETURN);
        ircd.events.register_client = create_event(EVENT_FL_NORETURN);
        ircd.events.unregister_client = create_event(EVENT_FL_NORETURN);
        ircd.events.client_nick = create_event(EVENT_FL_NORETURN);
        ircd.events.client_oper = create_event(EVENT_FL_NORETURN);
        ircd.events.client_deoper = create_event(EVENT_FL_NORETURN);
        ircd.events.channel_create = create_event(EVENT_FL_NORETURN);
        ircd.events.channel_destroy = create_event(EVENT_FL_NORETURN);
        ircd.events.channel_add = create_event(EVENT_FL_NORETURN);
        ircd.events.channel_del = create_event(EVENT_FL_NORETURN);
        ircd.events.server_introduce = create_event(EVENT_FL_NORETURN);
        ircd.events.server_establish = create_event(EVENT_FL_NORETURN);
        ircd.events.can_join_channel = create_event(EVENT_FL_CONDITIONAL);
        ircd.events.can_see_channel = create_event(EVENT_FL_CONDITIONAL);
        ircd.events.can_send_channel = create_event(EVENT_FL_CONDITIONAL);
        ircd.events.can_nick_channel = create_event(EVENT_FL_CONDITIONAL);
        ircd.events.can_send_client = create_event(EVENT_FL_CONDITIONAL);
        ircd.events.can_nick_client = create_event(EVENT_FL_CONDITIONAL);
    }
    ircd.events.started = create_event(EVENT_FL_HOOKONCE);

    /* grab the timer hook to see if connections have timed out */
    timer_ref = create_timer(-1, timer_fuzz, ircd_timer_hook, NULL);
    /* grab the 'afterpoll' hook to do write-outs */
    add_hook(me.events.afterpoll, ircd_writer_hook);
    /* grab reload events */
    add_hook(me.events.read_conf, ircd_reload_hook);
    /* grab module loads/unloads */
    add_hook(me.events.load_module, ircd_loadmodule_hook);
    add_hook(me.events.unload_module, ircd_loadmodule_hook);

    /* setup default character maps for nicks/channels/hosts/others */
    if (!get_module_savedata(savelist, "ircd.maps", &ircd.maps)) {
        istr_map("A-Z`^_[{]}\\| a-z", ircd.maps.nick_first);
        istr_map("A-Z0-9`^_[{]}\\|- a-z", ircd.maps.nick);
        /* it's pretty important that you NOT put a / in here.  It will break
         * ACLs and CIDR masks.  Similarly, don't put colons in.  While they
         * *are* valid for IPs, they are not for *hostnames* which is what is
         * important when using this map */
        istr_map("A-Z0-9.- a-z", ircd.maps.host);
        /* the channel map is more complicated.  allow all characters from 33 -
         * 159 (except the , char), and 161-255.  However, a-z == A-Z, so map
         * that first.  All in all this is about five or six ranges to
         * handle. */
        istr_map("A-Z\\033-\\043\\045-\\064\\091-\\096\\123-\\159\\161-\\255 a-z",
                ircd.maps.channel);
    }

    /* allocate the argv structure */
    ircd.argv = malloc(sizeof(char *) * COMMAND_MAXARGS);
    for (i = 0;i < COMMAND_MAXARGS;i++)
        ircd.argv[i] = malloc(COMMAND_MAXARGLEN + 1);

    /* fill in the 'messages' stuff */
    if (!get_module_savedata(savelist, "ircd.messages", &ircd.messages)) {
        LIST_ALLOC(ircd.messages.sets);
        ircd.messages.size = 2048;
        ircd.messages.count = 1000;
        ircd.messages.msgs = malloc(sizeof(message_t) * ircd.messages.size);
        memset(ircd.messages.msgs, 0, sizeof(message_t) * ircd.messages.size);

        /* create numerics for messages (lazy-style) */
        /* common numerics */
        CMSG("001", ":Welcome to %s %s!%s@%s");                    /* rpl_welcome */
        CMSG("002", ":Your host is %s, running version %s");/* rpl_yourhost */
        CMSG("003", ":This server was created %s");         /* rpl_created */
        CMSG("004", "%s %s %s %s");                         /* rpl_myinfo */
        CMSG("005", "%s :are available on this server.");   /* rpl_isupport */
        CMSG("010", "%s %d :Please redirect your client to this server and "
                "port."); /* rpl_redir */
        CMSG("263", ":Server load is temporarily too heavy, please wait a "
                "while and try again."); /* rpl_loadtoohigh */

       /* the error numerics */
        CMSG("401", "%s :No such nick/channel"); /* err_nosuchnick */
        CMSG("402", "%s :No such server");       /* err_nosuchserver */
        CMSG("403", "%s :No such channel");      /* err_nosuchchannel */
        /* err_toomanytargets */
        CMSG("407", "%s :Duplicate recipients, no message delivered.");
        CMSG("421", "%s :Unknown command.");     /* err_unknowncommand */
        CMSG("432", "%s :%s [%s]"); /* err_erroneousnickname */
        CMSG("441", "%s %s :They aren't on that channel.");
        CMSG("442", "%s :You're not on that channel"); /* err_notonchannel */
        CMSG("451", ":You have not registered"); /* err_notregistered */
        CMSG("461", "%s :Not enough parameters"); /* err_needmoreparams */
        CMSG("462", ":You may not reregister"); /* err_alreadyregistered */
        CMSG("464", ":Password Incorrect"); /* err_passwdmismatch */
        CMSG("479", "%s :Channel name contains illegal characters");
        CMSG("481", ":Permission denied.  "
                "You do not have the correct privileges");
        CMSG("485", "%s :Cannot %s channel (%s)");

        CMSG("771", "%s :%s");
    }
        
    if (!get_module_savedata(savelist, "ircd.hashes", &ircd.hashes)) {
        EXPORT_SYM(nickcmp);
        EXPORT_SYM(chancmp);

        ircd.hashes.command = create_hash_table(32,
                offsetof(struct command, name), COMMAND_MAXLEN,
                HASH_FL_NOCASE|HASH_FL_STRING, "strncasecmp");

        ircd.hashes.client = create_hash_table(128, offsetof(client_t, nick),
                NICKLEN, HASH_FL_NOCASE|HASH_FL_STRING, "nickcmp");
        ircd.hashes.client_history = create_hash_table(1024,
                offsetof(struct client_history, nick), NICKLEN,
                HASH_FL_NOCASE|HASH_FL_STRING, "nickcmp");

        ircd.hashes.channel = create_hash_table(128, offsetof(channel_t, name),
                CHANLEN, HASH_FL_NOCASE|HASH_FL_STRING, "chancmp");
    }

    if (!get_module_savedata(savelist, "ircd.mdext", &ircd.mdext)) {
        EXPORT_SYM(client_mdext_iter);
        EXPORT_SYM(channel_mdext_iter);
        EXPORT_SYM(class_mdext_iter);
        ircd.mdext.client = create_mdext_header("client_mdext_iter");
        ircd.mdext.channel = create_mdext_header("channel_mdext_iter");
        ircd.mdext.class = create_mdext_header("class_mdext_iter");
    }

    if (reload) {
        LIST_FOREACH(cp, ircd.connections.clients, lp)
            add_hook(cp->sock->datahook, ircd_connection_datahook);
        LIST_FOREACH(cp, ircd.connections.servers, lp)
            add_hook(cp->sock->datahook, ircd_connection_datahook);
        LIST_FOREACH(isp, ircd.lists.listeners, lp)
            add_hook(isp->datahook, ircd_listen_hook);
    }

    /* now parse our conf, but only if we're not just reloading.  reloads
     * shouldn't trigger a reparse (as this will tend to load modules when it
     * really shouldn't) */
    if (!reload && !ircd_parse_conf(conf))
        return 0;

    /* setup our default protocol (rfc1459). */
#define DEFAULT_PROTOCOL "rfc1459"
    if (find_protocol(DEFAULT_PROTOCOL) == NULL &&
        !add_protocol(DEFAULT_PROTOCOL)) {
        log_error("could not load rfc1459 protocol?");
        return 0;
    }
    ircd.default_proto = find_protocol(DEFAULT_PROTOCOL);

    if (!ircd.started) {
        ircd.started = 1; /* yay. */
        hook_event(ircd.events.started, NULL);
    }

    destroy_event(ircd.events.started);

    /* add ISUPPORT stuff */
    add_isupport("CHANTYPES", ISUPPORT_FL_STR, "#");
    add_isupport("CASEMAPPING", ISUPPORT_FL_STR, "ascii"); /* XXX: misnomer */
    add_isupport("NETWORK", ISUPPORT_FL_STR, ircd.network);

    ui64 = ircd.limits.nicklen;
    add_isupport("NICKLEN", ISUPPORT_FL_INT, (char *)&ui64);
    ui64 = ircd.limits.chanlen;
    add_isupport("CHANNELLEN", ISUPPORT_FL_INT, (char *)&ui64);

    return 1;
}

/* XXX: there may be some missing stuff here.  not sure. */
MODULE_UNLOADER(ircd) {
    int i;
    connection_t *cp, *cp2;
    isocket_t *isp, *isp2;

    destroy_timer(timer_ref);
    remove_hook(me.events.afterpoll, ircd_writer_hook);
    remove_hook(me.events.read_conf, ircd_reload_hook);
    remove_hook(me.events.load_module, ircd_loadmodule_hook);
    remove_hook(me.events.unload_module, ircd_loadmodule_hook);

    free(ircd.sends);
    for (i = 0;i < COMMAND_MAXARGS;i++)
        free(ircd.argv[i]);
    free(ircd.argv);

    dns_lookup_cancel(connection_lookup_hook);
    ident_cancel(connection_ident_hook);

    /* close unknown connections and either remove our datahook if we're
     * reloading, or simply close the other connections. */
    close_unknown_connections("module reload");
    cp = LIST_FIRST(ircd.connections.clients);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        if (reload)
            remove_hook(cp->sock->datahook, ircd_connection_datahook);
        else {
            cp->cli->flags |= IRCD_CLIENT_KILLED;
            destroy_connection(cp, "module unloaded");
        }
        cp = cp2;
    }
    cp = LIST_FIRST(ircd.connections.servers);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        if (reload)
            remove_hook(cp->sock->datahook, ircd_connection_datahook);
        else
            destroy_connection(cp, "module unloaded");
        cp = cp2;
    }
    /* don't forget to modify our listening sockets too */
    isp = LIST_FIRST(ircd.lists.listeners);
    while (isp != NULL) {
        isp2 = LIST_NEXT(isp, lp);
        if (reload)
            remove_hook(isp->datahook, ircd_listen_hook);
        else {
            LIST_REMOVE(isp, lp);
            destroy_socket(isp);
        }
        isp = isp2;
    }

    /* if we're reloading, do lots of preservation (we could just preserve the
     * whole ircd structure, except that then it would be a pain to put stuff
     * in it.  so we preserve a lot of smaller items.  also, a lot of the items
     * aren't worth preserving because we do a conf parse anyhow.  if an item
     * isn't preserved, it is probably just re-set at conf parse */
    if (reload) {
        add_module_savedata(savelist, "ircd.me", sizeof(ircd.me), &ircd.me);
        add_module_savedata(savelist, "ircd.address", sizeof(ircd.address),
                ircd.address);
        add_module_savedata(savelist, "ircd.network", sizeof(ircd.network),
                ircd.network);
        add_module_savedata(savelist, "ircd.network_full",
                sizeof(ircd.network_full), ircd.network_full);
        add_module_savedata(savelist, "ircd.version", sizeof(ircd.version),
                ircd.version);
        add_module_savedata(savelist, "ircd.vercomment",
                sizeof(ircd.vercomment), ircd.vercomment);
        add_module_savedata(savelist, "ircd.statsfile", sizeof(ircd.statsfile),
                ircd.statsfile);
        add_module_savedata(savelist, "ircd.started", sizeof(ircd.started),
                &ircd.started);
        add_module_savedata(savelist, "ircd.ascstart", sizeof(ircd.ascstart),
                ircd.ascstart);
        add_module_savedata(savelist, "ircd.stats", sizeof(ircd.stats),
                &ircd.stats);
        add_module_savedata(savelist, "ircd.limits", sizeof(ircd.limits),
                &ircd.limits);
        add_module_savedata(savelist, "ircd.umodes", sizeof(ircd.umodes),
                &ircd.umodes);
        add_module_savedata(savelist, "ircd.cmodes", sizeof(ircd.cmodes),
                &ircd.cmodes);
        add_module_savedata(savelist, "ircd.events", sizeof(ircd.events),
                &ircd.events);
        add_module_savedata(savelist, "ircd.connections",
                sizeof(ircd.connections), &ircd.connections);
        add_module_savedata(savelist, "ircd.hashes", sizeof(ircd.hashes),
                &ircd.hashes);
        add_module_savedata(savelist, "ircd.mdext", sizeof(ircd.mdext),
                &ircd.mdext);
        add_module_savedata(savelist, "ircd.maps", sizeof(ircd.maps),
                &ircd.maps);
        add_module_savedata(savelist, "ircd.messages", sizeof(ircd.messages),
                &ircd.messages);
        add_module_savedata(savelist, "ircd.sflag", sizeof(ircd.sflag),
                &ircd.sflag);
        add_module_savedata(savelist, "ircd.privileges",
                sizeof(ircd.privileges), &ircd.privileges);
        add_module_savedata(savelist, "ircd.lists", sizeof(ircd.lists),
                &ircd.lists);
    } else {
        /* we need to clear out a lot of events and memory (XXX: not doing this
         * yet. :/). */

        /* events ... */
        destroy_event(ircd.events.stage1_connect);
        destroy_event(ircd.events.stage2_connect);
        destroy_event(ircd.events.stage3_connect);
        destroy_event(ircd.events.client_connect);
        destroy_event(ircd.events.client_disconnect);
        destroy_event(ircd.events.register_client);
        destroy_event(ircd.events.unregister_client);
        destroy_event(ircd.events.client_nick);
        destroy_event(ircd.events.channel_create);
        destroy_event(ircd.events.channel_destroy);
        destroy_event(ircd.events.channel_add);
        destroy_event(ircd.events.channel_del);
        destroy_event(ircd.events.server_introduce);
        destroy_event(ircd.events.server_establish);
        destroy_event(ircd.events.can_join_channel);
        destroy_event(ircd.events.can_see_channel);
        destroy_event(ircd.events.can_send_channel);
        destroy_event(ircd.events.can_nick_channel);

        /* hash tables */
        destroy_hash_table(ircd.hashes.client);
        destroy_hash_table(ircd.hashes.client_history);
        destroy_hash_table(ircd.hashes.command);
        destroy_hash_table(ircd.hashes.channel);

        /* And lists... */
        LIST_FREE(ircd.connections.stage1);
        LIST_FREE(ircd.connections.stage2);
        LIST_FREE(ircd.connections.clients);
        LIST_FREE(ircd.connections.servers);

        LIST_FREE(ircd.lists.listeners);
        LIST_FREE(ircd.lists.servers);
        LIST_FREE(ircd.lists.server_connects);
        LIST_FREE(ircd.lists.clients);
        TAILQ_FREE(ircd.lists.client_history);
        LIST_FREE(ircd.lists.classes);
        LIST_FREE(ircd.lists.protocols);
        LIST_FREE(ircd.lists.channels);
        LIST_FREE(ircd.lists.commands);
        LIST_FREE(ircd.lists.isupport);
        LIST_FREE(ircd.lists.hostlists);
        LIST_FREE(ircd.lists.xinfo_handlers);
        LIST_FREE(ircd.lists.xattr);
    }
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
