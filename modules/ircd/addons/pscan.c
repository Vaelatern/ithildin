/*
 * pscan.c: ircd interface to proxyscan.so
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This mechanism performs network-wide proxy scans on connecting clients.  It
 * has an extensive configuration system, and makes use of 'proxyscan.so'
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "../../proxyscan/proxyscan.h"

IDSTRING(rcsid, "$Id: pscan.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd proxyscan
*/

HOOK_FUNCTION(pscan_started_hook);
HOOK_FUNCTION(pscan_privmsg_hook);
HOOK_FUNCTION(pscan_found_hook);
HOOK_FUNCTION(pscan_clean_hook);
HOOK_FUNCTION(pscan_client_hook);
HOOK_FUNCTION(pscan_log_hook);

struct pscan_data {
    client_t  *cli;
    channel_t *chan;
    conf_list_t **conf;        /* our configuration data */

    int            scanning;        /* set to 1 when scanning, 0 when not */
    int            bantime;        /* time to akill users for in seconds */
    char    *website;        /* website for akill info */

    /* message format indices */
    struct {
        int akillmsg;
    } formats;
} pscan;

MODULE_LOADER(pscan) {
    
    memset(&pscan, 0, sizeof(pscan));
    
    pscan.conf = confdata;
    if (!ircd.started) /* either hook when started, or right away if we are */
        add_hook(ircd.events.started, pscan_started_hook);
    else
        pscan_started_hook(NULL, NULL);

    return 1;
}

/* XXX: this still needs more rigorous error checking for user-conf values.
 * bleah bleah bleah. */
HOOK_FUNCTION(pscan_started_hook) {
    client_t *cli;
    channel_t *chan;
    conf_list_t *conf = *pscan.conf;
    char *s;
    int i;

    /* create our pseudo-client. ;) and hook for privmsgs. */
    pscan.cli = create_client(NULL);
    cli = pscan.cli;

    /* grab configuration bits. */
    s = conf_find_entry("nick", conf, 1);
    if (s != NULL)
        strcpy(cli->nick, s);
    else
        strcpy(cli->nick, "pscan");
    s = conf_find_entry("user", conf, 1);
    if (s != NULL)
        strcpy(cli->user, s);
    else
        strcpy(cli->user, "pscan");
    s = conf_find_entry("host", conf, 1);
    if (s != NULL)
        strcpy(cli->host, s);
    else
        strcpy(cli->host, ircd.me->name);
    s = conf_find_entry("info", conf, 1);
    if (s != NULL)
        strcpy(cli->info, s);
    else
        strcpy(cli->info, "proxy scan bot");
    strcpy(cli->ip, "127.0.0.1");
    cli->signon = cli->ts = me.now;
    cli->server = ircd.me; /* hopefully, with conn = NULL this won't
                                introduce any problems.  yeep. */
    
    hash_insert(ircd.hashes.client, cli); /* insert them into the hash */
    sptr = NULL; /* make sure that this data is propogated correctly! */
    register_client(cli);
    command_add_hook("PRIVMSG", 1, pscan_privmsg_hook,
            COMMAND_FL_EXCL_HOOK);

    /* have our client enter our channel, or create it if it doesn't exist.
     * whee. */
    s = conf_find_entry("channel", conf, 1);
    if (s == NULL)
        s = "#proxyscan";

    pscan.chan = find_channel(s);
    if (pscan.chan != NULL) {
        chan = pscan.chan;
        add_to_channel(cli, chan, true);
        sendto_serv_butone(NULL, cli, NULL, NULL, "SJOIN", "%d %s",
                chan->created, cli->nick);
    } else {
        pscan.chan = chan = create_channel(s);
        add_to_channel(cli, chan, true);
        chanmode_setprefix('@', chan, cli->nick, &i);
        sendto_serv_butone(NULL, cli, NULL, NULL, "SJOIN", "%d %s + :@%s",
                chan->created, chan->name, cli->nick);
    }

    /* grab other miscellaneous conf stuff */
    pscan.bantime = str_conv_time(conf_find_entry("bantime", conf, 1), 3600);
    s = conf_find_entry("website", conf, 1);
    if (s != NULL)
        pscan.website = strdup(s);
    else
        pscan.website = strdup("http://non.exist.ant");

    /* add some hooks here */
    add_hook(ircd.events.register_client, pscan_client_hook);
    add_hook(proxy_found, pscan_found_hook);
    add_hook(proxy_clean, pscan_clean_hook);

    /* logging goop */
    add_hook(me.events.log_notice, pscan_log_hook);
    add_hook(me.events.log_warn, pscan_log_hook);
    add_hook(me.events.log_error, pscan_log_hook);
    add_hook(me.events.log_unknown, pscan_log_hook);
    if (me.debug)
        add_hook(me.events.log_debug, pscan_log_hook);

    /* create the akill message */
    pscan.formats.akillmsg = create_message("pscan-akill",
            "open %s %s.%0.0s");
    return NULL;
}

HOOK_FUNCTION(pscan_privmsg_hook) {
    struct command_hook_args *hdata = (struct command_hook_args *)data;
    int argc __UNUSED = hdata->argc;
    char **argv = hdata->argv;
    client_t *cli = hdata->cli;
    char cmd[32], *args, *s;
    struct pscan_entry *pent;
    void *target;

    if (check_channame(argv[1]))
        target = pscan.chan;
    else
        target = cli;

    /* they're not an oper?  pfft. */
    if (target == cli && !usermode_isset(cli, 'o')) {
        sendto_one_from(cli, pscan.cli, NULL, "NOTICE",
                ":only IRC operators may use this client.");
    }
        
    /* parse message.  yuck.  */
    strncpy(cmd, argv[2], 31);
    cmd[31] = '\0';
    s = strchr(cmd, ' ');
    if (s != NULL)
        *s = '\0';
    args = strchr(argv[2], ' ');
    if (args == NULL)
        args = ""; /* set it to the empty string. */
    else {
        while (isspace(*args))
            args++; /* skip the space(s) */
    }

#if 0
    sendto_channel_butone(pscan.chan, pscan.cli, pscan.cli, NULL, "PRIVCMSG",
            ":received privmsg from %s!%s@%s: cmd=(%p)%s args=(%p)%s",
            cli->nick, cli->user, cli->host, cmd, cmd, args, args);
#endif

    if (!strcasecmp(cmd, "SCAN")) {
        if (!strncasecmp(args, "ON", 2)) {
            pscan.scanning = 1;
            sendto_channel_butone(pscan.chan, pscan.cli, pscan.cli, NULL,
                    "PRIVCMSG", ":%s!%s@%s turned scanning on", cli->nick,
                    cli->user, cli->host);
            sendto_one_from(cli, pscan.cli, NULL, "NOTICE",
                    ":turned scanning on");
        } else if (!strncasecmp(args, "OFF", 3)) {
            pscan.scanning = 0;
            sendto_channel_butone(pscan.chan, pscan.cli, pscan.cli, NULL,
                    "PRIVCMSG", ":%s!%s@%s turned scanning off", cli->nick,
                    cli->user, cli->host);
            sendto_one_from(cli, pscan.cli, NULL, "NOTICE",
                    ":turned scanning off");
        } else
            sendto_one_from(cli, pscan.cli, NULL, "NOTICE",
                    ":proxy scanning is %s",
                    (pscan.scanning ? "on" : "off"));
    } else if (!strcasecmp(cmd, "CHECK")) {
        if (!strcmp(args, "")) {
            sendto_one_from(cli, pscan.cli, NULL, "NOTICE",
                    ":usage: CHECK <address>");
        } else {
            sendto_channel_butone(pscan.chan, pscan.cli, pscan.cli, NULL,
                    "PRIVCMSG", ":%s!%s@%s requested scan for %s",
                    cli->nick, cli->user, cli->host, args);
            sendto_one_from(cli, pscan.cli, NULL, "NOTICE",
                    ":checking %s for open proxies...", args);
            /* pass an strdup of the client's nickname. */
            if ((pent = proxy_scan(args, PSCAN_FL_CHECKALL|
                            PSCAN_FL_NOTIFY_CLEAN|PSCAN_FL_NOCACHE,
                        target == cli ? strdup(cli->nick) : target)) == NULL) {
                sendto_one_from(cli, pscan.cli, NULL, "NOTICE",
                        ":couldn't scan %s for proxies.", args);
            }
        }
    }

    return NULL;
}

HOOK_FUNCTION(pscan_found_hook) {
    struct pscan_entry *pent = (struct pscan_entry *)data;
    char buf[256];
    int len = 0;
    int count = 0;
    client_t *cli;

    if (pent->flags & PSCAN_FL_SOCKS4_FOUND) {
        len += sprintf(buf + len, "%ssocks4", (len ? "/" : ""));
        count++;
    }
    if (pent->flags & PSCAN_FL_SOCKS5_FOUND) {
        len += sprintf(buf + len, "%ssocks5", (len ? "/" : ""));
        count++;
    }
    if (pent->flags & PSCAN_FL_TELNET_FOUND) {
        len += sprintf(buf + len, "%stelnet", (len ? "/" : ""));
        count++;
    }
    if (pent->flags & PSCAN_FL_HTTP_FOUND) {
        if (pent->flags & PSCAN_FL_HTTP8080_FOUND) {
            len += sprintf(buf + len, "%shttp8080", (len ? "/" : ""));
            count++;
        }
        if (pent->flags & PSCAN_FL_HTTP8000_FOUND) {
            len += sprintf(buf + len, "%shttp8000", (len ? "/" : ""));
            count++;
        }
        if (pent->flags & PSCAN_FL_HTTP3128_FOUND) {
            len += sprintf(buf + len, "%shttp3128", (len ? "/" : ""));
            count++;
        }
        if (pent->flags & PSCAN_FL_HTTP81_FOUND) {
            len += sprintf(buf + len, "%shttp81", (len ? "/" : ""));
            count++;
        }
        if (pent->flags & PSCAN_FL_HTTP80_FOUND) {
            len += sprintf(buf + len, "%shttp80", (len ? "/" : ""));
            count++;
        }
    }

    /* if it isn't the channel, and we find the client it points to, send a
     * notice privately.  otherwise move down below. */
    if (pent->udata != NULL && pent->udata != pscan.chan &&
            (cli = find_client(pent->udata)) != NULL) {
        sendto_one_from(cli, pscan.cli, NULL, "NOTICE",
                ":found open %s %s on %s", buf, 
                (count > 1 ? "proxies" : "proxy"), pent->addr);
        free(pent->udata);
        return NULL; /* we're done if this is just a user lookup */
    }

    sendto_channel_butone(pscan.chan, pscan.cli, pscan.cli,
            NULL, "PRIVCMSG", ":found open %s %s on %s", buf, 
            (count > 1 ? "proxies" : "proxy"), pent->addr);
    if (pent->udata == NULL) {
        char akillmsg[384];
        sendto_serv_butone(ircd.me, pscan.cli, NULL, NULL, "GLOBOPS",
                ":found open %s %s on %s.  setting one hour akill.", buf,
                (count > 1 ? "proxies" : "proxy"), pent->addr);
        snprintf(akillmsg, 384, MSG_FMT(pscan.cli, pscan.formats.akillmsg),
                buf, (count > 1 ? "proxies" : "proxy"), pscan.website);
        sendto_serv_butone(ircd.me, NULL, ircd.me, NULL, "AKILL",
                "%s * %d pscan %d :%s", pent->addr, pscan.bantime,
                me.now, akillmsg);
    }

    return NULL;
}

HOOK_FUNCTION(pscan_client_hook) {
    client_t *cli = (client_t *)data;
    struct pscan_entry *pent;

    if (pscan.scanning &&
            (pent = proxy_scan(cli->ip, 0, NULL)) != NULL) {
#if 0
        sendto_channel_butone(pscan.chan, pscan.cli, pscan.cli, NULL,
                "PRIVCMSG", ":Initiating proxy scan for %s[%s]...", cli->nick,
                cli->ip);
#endif
    }

    return NULL;
}

HOOK_FUNCTION(pscan_clean_hook) {
    struct pscan_entry *pent = (struct pscan_entry *)data;
    client_t *cli;

    /* given the way the flags work, we'll only get hooked if we asked for it.
     * on the other hand, don't assume pent->udata :) */
    if (pent->udata != NULL && pent->udata != pscan.chan && 
            (cli = find_client(pent->udata)) != NULL) {
        sendto_one_from(cli, pscan.cli, NULL, "NOTICE",
                ":no proxies found on %s", pent->addr);
        free(pent->udata);
        return NULL;
    }

    /* this may come from channel requests. */
    sendto_channel_butone(pscan.chan, pscan.cli, pscan.cli, NULL,
            "PRIVCMSG", ":No proxies found on %s", pent->addr);

    return NULL;
}

HOOK_FUNCTION(pscan_log_hook) {
    const char *name = NULL;

    if (ep == me.events.log_debug)
        name = "debug";
    else if (ep == me.events.log_notice)
        name = "notice";
    else if (ep == me.events.log_warn)
        name = "warning";
    else if (ep == me.events.log_error)
        name = "error";
    else
        name = "unknown";

    sendto_channel_butone(pscan.chan, pscan.cli, pscan.cli, NULL, "PRIVCMSG",
            ":[%s]: %s", name, (char *)data);

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
