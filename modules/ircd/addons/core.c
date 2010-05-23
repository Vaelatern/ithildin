/*
 * core.c: core server support module
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module handles setting up of a few mostly universal IRC server
 * constructs.  It loads all the commands necessary to provide core
 * functionality and synchronization across the server (by way of depends), and
 * also creates basic channel modes.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"
#include "commands/away.h"

IDSTRING(rcsid, "$Id: core.c 701 2006-02-26 01:30:57Z wd $");

MODULE_REGISTER("$Rev: 701 $");

/* the list of commands we require to support absolute *base* functionality,
 * this basically provides enough support to synchronize (on a basic leve) with
 * other servers. */
/*
@DEPENDENCIES@:        ircd
@DEPENDENCIES@: ircd/commands/error        ircd/commands/join
@DEPENDENCIES@: ircd/commands/kick        ircd/commands/kill
@DEPENDENCIES@: ircd/commands/mode        ircd/commands/nick
@DEPENDENCIES@: ircd/commands/part        ircd/commands/ping
@DEPENDENCIES@: ircd/commands/pong        ircd/commands/quit
@DEPENDENCIES@: ircd/commands/server        ircd/commands/sjoin
@DEPENDENCIES@: ircd/commands/squit
*/

/* data for this module */
struct core_addon_struct core;

/* channel mode handlers */
static int check_bans(const struct channel_ban_list *, const char *,
        const char *, const char *, const char *, const char *);
CHANMODE_FUNC(chanmode_ban);
CHANMODE_QUERY_FUNC(chanmode_ban_query);
HOOK_FUNCTION(can_join_mode_b);
HOOK_FUNCTION(can_send_mode_b);
HOOK_FUNCTION(can_nick_mode_b);

HOOK_FUNCTION(can_send_mode_m);
HOOK_FUNCTION(can_send_mode_n);
HOOK_FUNCTION(can_show_mode_s);

CHANMODE_FUNC(chanmode_uflag);
CHANMODE_QUERY_FUNC(chanmode_uflag_query);
HOOK_FUNCTION(can_act_mode_ov);

CHANMODE_FUNC(chanmode_key);
CHANMODE_QUERY_FUNC(chanmode_key_query);
HOOK_FUNCTION(can_join_mode_k);

CHANMODE_FUNC(chanmode_limit);
CHANMODE_QUERY_FUNC(chanmode_limit_query);
HOOK_FUNCTION(can_join_mode_l);

/* externs from away/topic */
struct mdext_item *away_mdext;
struct mdext_item *topic_mdext;

MODULE_LOADER(core) {
    uint64_t ui64 = 0;

    /* add various channel modes. */
    if (!get_module_savedata(savelist, "core", &core)) {
        core.privs.see_hidden_chan = create_privilege("see-hidden-channels",
                PRIVILEGE_FL_BOOL, &ui64, NULL);

        EXPORT_SYM(chanmode_ban);
        EXPORT_SYM(chanmode_ban_query);
        EXPORT_SYM(chanmode_key);
        EXPORT_SYM(chanmode_key_query);
        EXPORT_SYM(chanmode_limit);
        EXPORT_SYM(chanmode_limit_query);
        EXPORT_SYM(chanmode_flag);
        EXPORT_SYM(chanmode_flag_query);
        EXPORT_SYM(chanmode_uflag);
        EXPORT_SYM(chanmode_uflag_query);
        chanmode_request('b', &core.chanmodes.ban, CHANMODE_FL_A, "chanmode_ban",
                "chanmode_ban_query", sizeof(struct channel_ban_list), NULL);
        chanmode_request('k', &core.chanmodes.key, CHANMODE_FL_B, "chanmode_key",
                "chanmode_key_query", PASSWDLEN + 1, NULL);
        chanmode_request('l', &core.chanmodes.limit, CHANMODE_FL_C,
                "chanmode_limit", "chanmode_limit_query", sizeof(uint32_t), NULL);
        chanmode_request('m', &core.chanmodes.mod, CHANMODE_FL_D,
                "chanmode_flag", "chanmode_flag_query", 0, NULL);
        chanmode_request('n', &core.chanmodes.nextern, CHANMODE_FL_D,
                "chanmode_flag", "chanmode_flag_query", 0, NULL);
        chanmode_request('o', &core.chanmodes.op, CHANMODE_FL_PREFIX,
                "chanmode_uflag", "chanmode_uflag_query", 0, "@");
        chanmode_request('s', &core.chanmodes.secret, CHANMODE_FL_D,
                "chanmode_flag", "chanmode_flag_query", 0, NULL);
        chanmode_request('v', &core.chanmodes.voice, CHANMODE_FL_PREFIX,
                "chanmode_uflag", "chanmode_uflag_query", 0, "+");
    }

    add_hook(ircd.events.can_join_channel, can_join_mode_b);
    add_hook(ircd.events.can_send_channel, can_send_mode_b);
    add_hook(ircd.events.can_nick_channel, can_nick_mode_b);

    add_hook(ircd.events.can_send_channel, can_send_mode_m);

    add_hook(ircd.events.can_send_channel, can_send_mode_n);

    add_hook(ircd.events.can_see_channel, can_show_mode_s);

    add_hook(ircd.events.can_join_channel, can_join_mode_k);
    add_hook(ircd.events.can_join_channel, can_join_mode_l);

    add_hook(ircd.events.can_send_channel, can_act_mode_ov);
    add_hook(ircd.events.can_nick_channel, can_act_mode_ov);

    ui64 = MAX_BANS_PER_CHANNEL;
    add_isupport("MAXBANS", ISUPPORT_FL_INT, (char *)&ui64);

    /* and numerics */
#define CMSG create_message
    CMSG("334", ":%s");
    CMSG("367", "%s %s!%s@%s %s %lu");
    CMSG("368", "%s :End of Channel Ban List");
    CMSG("404", "%s :Cannot send to channel");
    CMSG("435", "%s %s :Cannot change to a banned nickname");
    CMSG("437", "%s :Cannot change nickname while banned or moderated on "
            "channel.");
    CMSG("471", "%s :Cannot join channel (+l)");
    CMSG("474", "%s :Cannot join channel (+b)");
    CMSG("475", "%s :Cannot join channel (+k)");
    CMSG("478", "%s %s :Channel ban list is full");
    CMSG("482", "%s :You're not a channel operator");
#undef CMSG

    return 1;
}

MODULE_UNLOADER(core) {

    if (reload)
        add_module_savedata(savelist, "core", sizeof(core),
                &core);
    else {
        destroy_privilege(core.privs.see_hidden_chan);
        chanmode_release(core.chanmodes.ban);
        chanmode_release(core.chanmodes.key);
        chanmode_release(core.chanmodes.limit);
        chanmode_release(core.chanmodes.mod);
        chanmode_release(core.chanmodes.nextern);
        chanmode_release(core.chanmodes.op);
        chanmode_release(core.chanmodes.secret);
        chanmode_release(core.chanmodes.voice);
    }

    remove_hook(ircd.events.can_join_channel, can_join_mode_b);
    remove_hook(ircd.events.can_send_channel, can_send_mode_b);
    remove_hook(ircd.events.can_nick_channel, can_nick_mode_b);

    remove_hook(ircd.events.can_send_channel, can_send_mode_m);

    remove_hook(ircd.events.can_send_channel, can_send_mode_n);

    remove_hook(ircd.events.can_see_channel, can_show_mode_s);

    remove_hook(ircd.events.can_join_channel, can_join_mode_k);
    remove_hook(ircd.events.can_join_channel, can_join_mode_l);

    remove_hook(ircd.events.can_send_channel, can_act_mode_ov);
    remove_hook(ircd.events.can_nick_channel, can_act_mode_ov);

    del_isupport(find_isupport("MAXBANS"));

    DMSG(RPL_COMMANDSYNTAX);
    DMSG(RPL_BANLIST);
    DMSG(RPL_ENDOFBANLIST);
    DMSG(ERR_CANNOTSENDTOCHAN);
    DMSG(ERR_BANONCHAN);
    DMSG(ERR_BANNICKCHANGE);
    DMSG(ERR_CHANNELISFULL);
    DMSG(ERR_BANNEDFROMCHAN);
    DMSG(ERR_BADCHANNELKEY);
    DMSG(ERR_BANLISTFULL);
    DMSG(ERR_CHANOPRIVSNEEDED);
}

/* Function to count how many bans in a channel a user matches against.  
 * We try to do the least number of comparisons possible (always 2,
 * maybe 3(. */

static int check_bans(const struct channel_ban_list *list, const char *nick,
        const char *user, const char *host, const char *ip,
        const char *orighost) {
    struct channel_ban *cbp;
    bool check_orighost = false;
    int bans = 0;

    if (orighost != NULL && *orighost != '\0' && strcasecmp(host, orighost))
        check_orighost = true;

    /* Walk the list only once, checking each ban against the various
     * pieces of data provided.  If the nick or username don't match we skip
     * right away, then do two (or three) host checks.  First we check the
     * "display" host, then we do ip matching on the ip, and then we check
     * the orighost if required. */

    LIST_FOREACH(cbp, list, lp) {
        if (!match(cbp->nick, nick) || !match(cbp->user, user))
            continue; /* not a match */
        if (match(cbp->host, host) || ipmatch(cbp->host, ip)) {
            bans++;
            continue;
        } else if (check_orighost && match(cbp->host, orighost)) {
            bans++;
            continue;
        }
    }

    return bans;
}

/* bans.  rather complicated! */
CHANMODE_FUNC(chanmode_ban) {
    struct channel_ban_list *banlist =
        (struct channel_ban_list *)chanmode_getdata(chan, mode);
    struct channel_ban *cbp;
    struct chanlink *clp;
    char nick[NICKLEN + 1];
    char user[USERLEN + 1];
    char host[HOSTLEN + 1];

    int cnt = 0;

    /* if we're not just clearing the data, check arg, and make sure they know
     * the argument is being used. */
    if (set != CHANMODE_CLEAR) {
        char *bang, *at;
        size_t nicklen, userlen, hostlen;

        if (arg == NULL) {
            /* if this is a client, send them the list of bans. */
            if (cli != NULL && MYCLIENT(cli)) {
                LIST_FOREACH(cbp, banlist, lp) {
                    sendto_one(cli, RPL_FMT(cli, RPL_BANLIST), chan->name,
                            cbp->nick, cbp->user, cbp->host, cbp->who,
                            cbp->when);
                }
                sendto_one(cli, RPL_FMT(cli, RPL_ENDOFBANLIST), chan->name);
            }
            return CHANMODE_NOP;
        }

        *argused = 1;
        /* if they're not ops, deny deny deny. */
        if (cli != NULL && !CHANOP(cli, chan) && !CLIENT_MASTER(cli))
            return ERR_CHANOPRIVSNEEDED;

        bang = strchr(arg, '!');
        at = strchr(arg, '@');
        /* here's the scoop: the bang can't be the first character of the
         * mask.  the at can't be the last character, and there has to be
         * something between the bang and the at. */
        if (bang == NULL || at == NULL || bang == arg ||
                *(at + 1) == '\0' || (bang + 1) == at || at < bang)
            return CHANMODE_FAIL; /* bogus mask */

        nicklen = bang - arg; /* length of text from beginning of the string
                                 to the '!' symbol. */
        userlen = at - (bang + 1); /* length from after the '!' to the '@'
                                    symbol */
        hostlen = strlen(at + 1); /* length from after the '@' to the end */

        /* Make sure nothing is too long, too */
        if (nicklen > NICKLEN || userlen > USERLEN || hostlen > HOSTLEN)
            return CHANMODE_FAIL; /* arguments are too long */

        strlcpy(nick, arg, nicklen + 1);
        strlcpy(user, bang + 1, userlen + 1);
        strlcpy(host, at + 1, hostlen + 1);
    }

    switch (set) {
        case CHANMODE_SET:
            LIST_FOREACH(cbp, banlist, lp) {
                if (++cnt > MAX_BANS_PER_CHANNEL && cli != NULL &&
                        MYCLIENT(cli)) {
                    sendto_one(cli, RPL_FMT(cli, ERR_BANLISTFULL),
                            chan->name, arg);
                    return CHANMODE_FAIL; /* no siree bob */
                }
                /* Previously I had tried to match the ban being set against
                 * other bans, but I'm not sure this behavior is desired so
                 * I'm taking it out for now... */
#if 0
                /* we only want to know if current bans cover this ban, not
                 * if this ban covers current ones, since it may cover
                 * other things too. */
                if (match(cbp->nick, nick) && match(cbp->user, user) &&
                        match(cbp->host, host))
                    return CHANMODE_FAIL; /* already set. */
#else
                if (!strcasecmp(cbp->nick, nick) &&
                        !strcasecmp(cbp->user, user) &&
                        !strcasecmp(cbp->host, host))
                    return CHANMODE_FAIL;
#endif
            }
            
            cbp = calloc(1, sizeof(struct channel_ban));
            strlcpy(cbp->nick, nick, NICKLEN + 1);
            strlcpy(cbp->user, user, USERLEN + 1);
            strlcpy(cbp->host, host, HOSTLEN + 1);
            if (cli != NULL)
                sprintf(cbp->who, "%s!%s@%s", cli->nick, cli->user, cli->host);
            else
                strcpy(cbp->who, ircd.me->name);
            cbp->when = me.now;
            cbp->type = CHANNEL_BAN_BAN;

            LIST_INSERT_HEAD(banlist, cbp, lp);

            /* count bans against all users in the channel. */
            LIST_FOREACH(clp, &chan->users, lpchan) {
                clp->bans = check_bans(banlist, clp->cli->nick,
                        clp->cli->user, clp->cli->host, clp->cli->ip,
                        clp->cli->orighost);
            }
            
            break;
        case CHANMODE_UNSET:
            LIST_FOREACH(cbp, banlist, lp) {
                if (!strcasecmp(cbp->nick, nick) &&
                        !strcasecmp(cbp->user, user) &&
                        !strcasecmp(cbp->host, host)) {
                    /* a winner. */
                    LIST_REMOVE(cbp, lp);
                    free(cbp);

                    /* count bans against all users in the channel. */
                    LIST_FOREACH(clp, &chan->users, lpchan) {
                        clp->bans = check_bans(banlist, clp->cli->nick,
                                clp->cli->user, clp->cli->host, clp->cli->ip,
                                clp->cli->orighost);
                    }
                    break;
                }
            }
            break;
        case CHANMODE_CLEAR:
            while ((cbp = LIST_FIRST(banlist)) != NULL) {
                LIST_REMOVE(cbp, lp);
                free(cbp);
            }
            break;
    }

    return CHANMODE_OK; /* a-okay. */
}

struct ban_query_state {
    bool started;
    struct channel_ban *curban;
};

CHANMODE_QUERY_FUNC(chanmode_ban_query) {
    struct channel_ban_list *banlist =
        (struct channel_ban_list *)chanmode_getdata(chan, mode);
    struct channel_ban *cbp;
    struct ban_query_state *bqs;
    char mask[BAN_MASK_LEN];
    size_t len;

    if (*state == NULL)
        *state = calloc(1, sizeof(struct ban_query_state));
    bqs = *state;

    if (bqs->started == false) {
        bqs->curban = LIST_FIRST(banlist);
        bqs->started = true;
    }
    
    /* always run this check because we might be done when we start.. */
    if (bqs->curban == NULL) {
        /* we're done here.. free bqs and let them leave the loop */
        free(bqs);
        *argused = 0;
        return CHANMODE_FAIL; /* all done! */
    }

    /* okay, we know we've either started or we finished and went home.. */
    cbp = bqs->curban;

    len = sprintf(mask, "%s!%s@%s", cbp->nick, cbp->user, cbp->host);
    if (len > (size_t)*argused) {
        *argused = -(len - *argused);
        /* do not advance the list as the consumer is expected not to use
         * this mode since they have no room for it. */
    } else {
        strcpy(arg, mask);
        *argused = len;
        /* advance the list since they promised us enough space! */
        bqs->curban = LIST_NEXT(cbp, lp);
    }

    return CHANMODE_OK;
}


HOOK_FUNCTION(can_join_mode_b) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;
    struct channel_ban_list *banlist =
        (struct channel_ban_list *)chanmode_getdata(ccap->chan,
                                                    core.chanmodes.ban);
    void *ret = (void *)HOOK_COND_OK; /* deny by default. */

    ccap->clp->bans = check_bans(banlist, ccap->cli->nick, ccap->cli->user,
            ccap->cli->host, ccap->cli->ip, ccap->cli->orighost);

    if (ccap->clp->bans)
        return (void *)ERR_BANNEDFROMCHAN;

    return ret; /* and return.. */
}
HOOK_FUNCTION(can_send_mode_b) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;

    if (ccap->clp == NULL)
        return (void *)HOOK_COND_NEUTRAL; /* doesn't effect us */
    if (ccap->clp->bans)
        return (void *)ERR_CANNOTSENDTOCHAN; /* banned, cannot send */

    return (void *)HOOK_COND_NEUTRAL;
}
HOOK_FUNCTION(can_nick_mode_b) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;
    struct channel_ban_list *banlist =
        (struct channel_ban_list *)chanmode_getdata(ccap->chan,
                                                    core.chanmodes.ban);

    if (ccap->clp == NULL) {
        log_debug("can_nick_mode_b() called when client wasn't in channel!");
        return (void *)HOOK_COND_NEUTRAL; /* not interested.. */
    }
    if (ccap->clp->bans)
       return (void *)ERR_BANNICKCHANGE; /* hope they handle this right. :) */

    /* if they're not banned, make sure the nickname change wouldn't result in
     * a ban either. */
    if (check_bans(banlist, ccap->extra, ccap->cli->user, ccap->cli->host,
                ccap->cli->ip, ccap->cli->orighost))
        return (void *)ERR_BANONCHAN;

    return (void *)HOOK_COND_NEUTRAL; /* eh. */
}

CHANMODE_FUNC(chanmode_flag) {
    
    *argused = 0;
    if (cli != NULL && !CHANOP(cli, chan) && !CLIENT_MASTER(cli))
        return ERR_CHANOPRIVSNEEDED;

    switch (set) {
        case CHANMODE_SET:
            chanmode_setflag(chan, mode);
            break;
        case CHANMODE_UNSET:
            chanmode_unsetflag(chan, mode);
            break;
    }
    return CHANMODE_OK;
}
CHANMODE_QUERY_FUNC(chanmode_flag_query) {
    *argused = 0;

    if (*state == NULL)
        *state = (void *)0x1; /* dummy pointer to nowhere */
    else
        return CHANMODE_FAIL; /* they already asked */

    if (!chanmode_isset(chan, mode))
        return CHANMODE_FAIL; /* not set */
    return CHANMODE_OK; /* it's set */
}


HOOK_FUNCTION(can_send_mode_m) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;

    if (chanmode_isset(ccap->chan, core.chanmodes.mod))
        return (void *)ERR_CANNOTSENDTOCHAN; /* unless overriden, no. */
    return (void *)HOOK_COND_NEUTRAL;
}
HOOK_FUNCTION(can_send_mode_n) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;

    /* if we're +n and they're not in the channel, don't let them send. */
    if (chanmode_isset(ccap->chan, core.chanmodes.nextern) &&
            ccap->clp == NULL)
        return (void *)ERR_CANNOTSENDTOCHAN;
    return (void *)HOOK_COND_NEUTRAL;
}
HOOK_FUNCTION(can_show_mode_s) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;

    /* if they're in the channel, it's always okay.  if the channel is +s it's
     * not okay unless they've got the see-hidden-channels privilege, and then
     * it's okay if they're opered.  if the channel isn't +s it's always okay
     * too. */
    if (ccap->clp != NULL)
        return (void *)HOOK_COND_OK; /* s'not a problem. */
    else if (chanmode_isset(ccap->chan, core.chanmodes.secret)) {
        if (OPER(ccap->cli) && BPRIV(ccap->cli, core.privs.see_hidden_chan))
            return (void *)HOOK_COND_ALWAYSOK; /* okay, but sketchy. */
        else
            return (void *)ERR_NOTONCHANNEL;
    } else
        return (void *)HOOK_COND_OK; /* it's not +s, okay by us. */
}

CHANMODE_FUNC(chanmode_key) {

    if (set != CHANMODE_CLEAR) {
        if (arg == NULL)
            return CHANMODE_NOARG;
        if (cli != NULL && !CHANOP(cli, chan) && !CLIENT_MASTER(cli))
            return ERR_CHANOPRIVSNEEDED;
        *argused = 1;
    }

    switch (set) {
        case CHANMODE_SET:
            if (strchr(arg, ' ') != NULL || strchr(arg, ',') != NULL)
                return CHANMODE_FAIL; /* no spaces or commas in keys */
            if (cli == NULL && chanmode_isset(chan, mode)) {
                /* this is coming from a server... we do something silly here.
                 * if we have a key currently, we compare the two keys and use
                 * the one which is lexocographically greater (pretty
                 * arbitrary, eh?) */
                if (strcmp(arg, chanmode_getstrdata(chan, mode)) <= 0)
                    strncpy(chanmode_getstrdata(chan, mode), arg, PASSWDLEN);
                return CHANMODE_OK;
            }

            strncpy(chanmode_getstrdata(chan, mode), arg, PASSWDLEN);
            chanmode_setflag(chan, mode);
            break;
        case CHANMODE_UNSET:
            memset(chanmode_getstrdata(chan, mode), 0, PASSWDLEN + 1);
            chanmode_unsetflag(chan, mode);
            break;
    }

    return CHANMODE_OK;
}
CHANMODE_QUERY_FUNC(chanmode_key_query) {
    size_t len;

    if (*state == NULL)
        *state = (void *)0x1; /* set it so we don't return more than once */
    else
        return CHANMODE_FAIL; /* already queried */

    if (!chanmode_isset(chan, mode)) {
        *argused = 0;
        return CHANMODE_FAIL; /* not set */
    } else {
        if ((len = strlen(chanmode_getstrdata(chan, mode))) >
                (size_t)*argused) {
            *argused = -(len - *argused);
            *state = NULL; /* they will have to ask again.. */
        } else {
            strncpy(arg, chanmode_getstrdata(chan, mode), PASSWDLEN);
            *argused = len;
        }
    }
    return CHANMODE_OK; /* it's set */
}


/* this function is hooked by the can_join event.  just compare the arg (if
 * any) to the key, and see if it's okay. */
HOOK_FUNCTION(can_join_mode_k) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;
    char *key = chanmode_getstrdata(ccap->chan, core.chanmodes.key);
    char *next, *cur;

    /* ccap->extra should be a comma separated list of keys (or NULL).  we try
     * each one in succession. */
    if (*key != '\0') {
        if (ccap->extra == NULL || *ccap->extra == '\0')
            return (void *)ERR_BADCHANNELKEY;
        cur = ccap->extra;
        next = strchr(cur, ',');
        while (cur != NULL) {
            if (!strncasecmp(key, cur,
                        (next == NULL ? PASSWDLEN : next - cur)))
                return (void *)HOOK_COND_OK; /* a match */
            if (next != NULL && *(next + 1) != '\0') {
                cur = next + 1;
                next = strchr(cur, ',');
            } else
                break;
        }
        return (void *)ERR_BADCHANNELKEY; /* no match, or no key */
    }

    return (void *)HOOK_COND_OK; /* okay to join. */
}

CHANMODE_FUNC(chanmode_limit) {
    uint32_t limit;
    uint32_t *chl = (uint32_t *)chanmode_getdata(chan, mode);

    if (cli != NULL && !CHANOP(cli, chan) && !CLIENT_MASTER(cli))
        return ERR_CHANOPRIVSNEEDED;
    switch (set) {
        case CHANMODE_SET:
            if (arg == NULL)
                return CHANMODE_NOARG;
            *argused = 1;
            limit = str_conv_int(arg, 0);

            /* if the limit is bogus don't bother. */
            if (limit == 0)
                return CHANMODE_FAIL; /* bad number */
            
            *chl = limit;
            chanmode_setflag(chan, mode);
            break;
        case CHANMODE_UNSET:
            *chl = 0;
            chanmode_unsetflag(chan, mode);
            *argused = 0;
    }
  
    return CHANMODE_OK;
}

CHANMODE_QUERY_FUNC(chanmode_limit_query) {
    char buf[16];
    size_t len;
    uint32_t *chl = (uint32_t *)chanmode_getdata(chan, mode);

    if (*state == NULL)
        *state = (void *)0x1; /* set it so we don't return more than once */
    else
        return CHANMODE_FAIL; /* already queried */
    if (!chanmode_isset(chan, mode))
        return CHANMODE_FAIL; /* not set */
    len = sprintf(buf, "%u", *chl);
    if (len > (uint32_t)*argused) {
        *argused = -(len - *argused);
        *state = NULL; /* they must ask again */
        return CHANMODE_OK;
    }
    strcpy(arg, buf);
    *argused = len;
    return CHANMODE_OK; /* it's set */
}


HOOK_FUNCTION(can_join_mode_l) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;

    /* if a limit is set and the number of people in the channel is greater
     * than the limit, don't let them in. */
    if (chanmode_getintdata(ccap->chan, core.chanmodes.limit) &&
            ccap->chan->onchannel >
            chanmode_getintdata(ccap->chan, core.chanmodes.limit))
        return (void *)ERR_CHANNELISFULL;

    return (void *)HOOK_COND_OK;
}

CHANMODE_FUNC(chanmode_uflag) {
    struct chanlink *clp;
    client_t *cp;

    if (cli != NULL && !CHANOP(cli, chan) && !CLIENT_MASTER(cli))
        return ERR_CHANOPRIVSNEEDED;

    /* error check stuff first */
    if (set != CHANMODE_CLEAR) {
        if (arg == NULL)
            return CHANMODE_NOARG;
        *argused = 1; /* otherwise we use the argument */
    } else
        return CHANMODE_OK; /* we don't have anything to clear */

    /* chase for +o/+v to avoid desynchs */
    cp = client_get_history(arg, 0);
    if (cp == NULL) {
        if (cli != NULL)
            sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHNICK), arg);
        return CHANMODE_FAIL;
    }
    if (arg != cp->nick)
        strcpy(arg, cp->nick); /* Maintain exact case of nickname if the arg
                                  came from somewhere but the client's nick */

    clp = find_chan_link(cp, chan);
    if (clp == NULL) {
        if (cli != NULL)
            sendto_one(cli, RPL_FMT(cli, ERR_USERNOTINCHANNEL), cp->nick,
                    chan->name);
        return CHANMODE_FAIL;
    }

    switch (set) {
        case CHANMODE_SET:
            clp->flags |= ircd.cmodes.modes[mode].umask;
            return CHANMODE_OK;
        case CHANMODE_UNSET:
            clp->flags &= ~ircd.cmodes.modes[mode].umask;
            return CHANMODE_OK;
    }
    
    return CHANMODE_FAIL;
}

CHANMODE_QUERY_FUNC(chanmode_uflag_query) {
    *argused = 0;

    return CHANMODE_FAIL; /* you can't query these.. */
}

HOOK_FUNCTION(can_act_mode_ov) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;

    if (ccap->clp == NULL)
        return (void *)HOOK_COND_NEUTRAL;
    if (CLINKOP(ccap->clp) || CLINKVOICE(ccap->clp))
        return (void *)HOOK_COND_ALWAYSOK;  /* if they're op'd or voiced,
                                               they can always change nicks
                                               or send. */
    return (void *)HOOK_COND_NEUTRAL; /* not interested. */
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
