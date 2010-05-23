/*
 * who.c: the WHO command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"
#include "commands/away.h"

IDSTRING(rcsid, "$Id: who.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/core
*/

static int priv_wholimit;
static int priv_whoinvis;

MODULE_LOADER(who) {
    int64_t i;

    /* privileges */
    i = 200;
    priv_wholimit = create_privilege("who-reply-limit", PRIVILEGE_FL_INT, &i,
            NULL);
    i = 0;
    priv_whoinvis = create_privilege("who-see-invisible", PRIVILEGE_FL_BOOL,
            &i, NULL);

    /* now create numerics */
#define RPL_ENDOFWHO 315
    CMSG("315", "%s :End of /WHO list.");
#define RPL_WHOREPLY 352
    CMSG("352", "%s %s %s %s %s %s :%d %s");
#define ERR_WHOSYNTAX 522
    CMSG("522", ":/WHO syntax incorrect, use /who ? for help.");
#define ERR_WHOLIMEXCEED 523
    CMSG("523", ":/who reply limit of %d exceeded.  Please narrow your "
            "search down and try again.");

    return 1;
}
MODULE_UNLOADER(who) {

    destroy_privilege(priv_wholimit);
    destroy_privilege(priv_whoinvis);

    /* now create numerics */
    DMSG(RPL_ENDOFWHO);
    DMSG(RPL_WHOREPLY);
    DMSG(ERR_WHOSYNTAX);
    DMSG(ERR_WHOLIMEXCEED);
}

/* this code is based largely on my /who command for DALnet (bahamut), with
 * modifications where I thought it might be useful. */

/* this is a structure used to store the query options passed to the /who
 * command by the user.  It is cleared at each use of /who. */
static struct {
    uint64_t usermodes;
    char *nick;
    char *user;
    char *host;
    char *gcos;
    char *ip;
    channel_t *channel;
    server_t *server;
    /* below here are single bit flags */
    struct {
        char umode:1;
        char check_umode:1;
        char nick:1;
        char user:1;
        char host:1;
        char gcos:1;
        char ip:1;
        char channel:1;
        char server:1;
        char away:1;
        char check_away:1;
        char show_chan:1;
        char search_chan:1;
        char spare:3; /* for other options later? .. */
    } flags;

    client_t *issuer;
} who_opts;

/* this is sent to the user if they send an invalid who request. */
static char *who_usage[] = {
"/WHO [+|-][acCghimMnsu] [args]",
"The flags are specified similar to channel modes, and some",
"have arguments (cghimnsu all take arguments).  Flags are set",
"to a positive check by +, and a negative check by -, and are",
"detailed as follows:",
"- a:                 user is away",
"- c <channel>:       user i on channel <channel> (no wildcards)",
"- C:                 shows first visible channel user is in",
"- g <gcos/realname>: user has string <gcos> in their GCOS field.",
"                     (wildcards accepted, operator only)",
"- h <hostname>:      user has string <host> in their hostname",
"                     (wildcards accepted)",
"- i <ip>:            user has IP address matching <ip>",
"                     (pattern, cidr, operator only)",
"- m <usermodes>:     user has modes <usermodes> set on them",
"                     (only 'o' for non-operators)",
"- M:                 check only in channels I am in",
"- n <nick>:          user has string <nick> in their nickname",
"                     (wildcards accepted)",
"- s <server>:        user is on server <server> (no wildcards)",
"- u <username>:      user has string <username> in their username",
"                     (wildcards accepted)",
NULL
};

/* macro for who_parse_syntax to break out. */
#define WHO_PARSE_SERROR(arg) do {                                            \
    sendto_one(cli, RPL_FMT(cli, ERR_WHOSYNTAX));                             \
    sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHO), arg);                         \
    return COMMAND_WEIGHT_MEDIUM;                                             \
} while (0)

/* this function parses the search options given by the user into the
 * 'who_opts' structure above.  If the options are not parseable or do not make
 * sense, we send an error message and return 0. */
static int who_parse_options(client_t *cli, int argc, char **argv) {
    char *flags, change = 1;
    unsigned char *s;
    int oarg = 1;

    memset(&who_opts, 0, sizeof(who_opts));

    who_opts.issuer = cli;
    /* if the user either sends no arguments or sends a single '?', send them
     * the help data and return. */
    if (!argc || *argv[0] == '?') {
        char **u = who_usage;
        while (*u != NULL)
            sendto_one(cli, RPL_FMT(cli, RPL_COMMANDSYNTAX), *u++);
        sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHO), "?");
        return 0;
    } else if (!strcmp(argv[0], "0")) {
        /* support /who 0 notation from days of yore. */
        if (argc > 1 && !strcasecmp(argv[1], "o")) {
            /* /who 0 o found opers. :) */
            who_opts.flags.check_umode = 1;
            who_opts.flags.umode = 1;
            who_opts.usermodes = ircd.umodes.modes['o'].mask;
        }
        who_opts.host = "*";
        who_opts.flags.host = 1;
        return 1;
    } else if (*argv[0] != '+' && *argv[0] != '-') {
        if (check_channame(argv[0])) {
            /* if it's a channel ... */
            if ((who_opts.channel = find_channel(argv[0])) == NULL) {
                sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHCHANNEL), argv[0]);
                sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHO), argv[0]);
                return 0;
            }
            who_opts.flags.channel = 1;
        } else if (strchr(argv[0], '.') != NULL) {
            /* if it's a hostname ... */
            who_opts.flags.host = 1;
            who_opts.host = argv[0];
        } else {
            /* otherwise, assume it's a nick */
            who_opts.flags.nick = 1;
            who_opts.nick = argv[0];
        }
        return 1; /* okay, try this.. */
    }

    /* well, none of the 'quick' conditions above were successful, so parse the
     * arguments and fill in who_opts. */
    flags = argv[0];
    while (*flags) {
        switch (*flags) {
            case '+':
                change = 1;
                break;
            case '-':
                change = 0;
                break;
            case 'a': /* away */
                who_opts.flags.away = change;
                who_opts.flags.check_away = 1;
                break;
            case 'c': /* channel */
                if (oarg >= argc || change == 0)
                    WHO_PARSE_SERROR(argv[0]);
                if ((who_opts.channel = find_channel(argv[oarg])) == NULL) {
                    /* couldn't find the channel.. */
                    sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHCHANNEL),
                            argv[oarg]);
                    sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHO),
                            argv[oarg]);
                    return 0;
                }
                who_opts.flags.channel = 1;
                oarg++;
                break;
            case 'C': /* show channel */
                who_opts.flags.show_chan = change;
                break;
            case 'g': /* gcos field search */
                if (!OPER(cli) || oarg >= argc)
                    WHO_PARSE_SERROR(argv[0]);
                who_opts.gcos = argv[oarg++];
                who_opts.flags.gcos = change;
                break;
            case 'h': /* hostname field search */
                if (oarg >= argc)
                    WHO_PARSE_SERROR(argv[0]);
                who_opts.host = argv[oarg++];
                who_opts.flags.host = change;
                break;
            case 'i': /* IP address search */
                if (!OPER(cli) || oarg >= argc)
                    WHO_PARSE_SERROR(argv[0]);
                who_opts.ip = argv[oarg++];
                who_opts.flags.ip = change;
                break;
            case 'm': /* usermode search */
                if (oarg >= argc)
                    WHO_PARSE_SERROR(argv[0]);
                s = (unsigned char *)argv[oarg++];
                while (*s) {
                    if (!ircd.umodes.modes[*s].avail &&
                            ircd.umodes.modes[*s].mask)
                        who_opts.usermodes |= ircd.umodes.modes[*s].mask;
                    s++;
                }
                if (!OPER(cli)) /* restrict them to only +o */
                    who_opts.usermodes = (who_opts.usermodes &
                            ircd.umodes.modes['o'].mask);
                if (who_opts.usermodes) {
                    who_opts.flags.check_umode = 1;
                    who_opts.flags.umode = change;
                }
                break;
            case 'M': /* search channels */
                who_opts.flags.search_chan = change;
                break;
            case 'n': /* nickname search */
                if (oarg >= argc)
                    WHO_PARSE_SERROR(argv[0]);
                who_opts.nick = argv[oarg++];
                who_opts.flags.nick = change;
                break;
            case 's':
                if (oarg >= argc)
                    WHO_PARSE_SERROR(argv[0]);
                if ((who_opts.server = find_server(argv[oarg])) == NULL) {
                    sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHSERVER),
                            argv[oarg]);
                    sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHO),
                            argv[oarg]);
                    return 0;
                }
                who_opts.flags.server = change;
                oarg++;
                break;
            case 'u': /* username search */
                if (oarg >= argc)
                    WHO_PARSE_SERROR(argv[0]);
                who_opts.user = argv[oarg++];
                who_opts.flags.user = change;
                break;
        }
        flags++;
    } /* end of that while loop way back there */

    /* now we have to look at 'who_opts' to see if the user supplied sensible
     * search options. */

    /* if they asked for 'search_chan' but nothing else, complain.  Specifying
     * only a channel makes no sense, and specifying only a nick also makes no
     * sense. */
    if (who_opts.flags.search_chan &&
            !(who_opts.flags.check_away || who_opts.gcos != NULL ||
                who_opts.host != NULL || who_opts.flags.check_umode ||
                who_opts.server != NULL || who_opts.user != NULL)) {
        if (oarg >= argc || who_opts.channel != NULL ||
                who_opts.nick != NULL || check_channame(argv[oarg]))
            WHO_PARSE_SERROR(argv[0]); /* blech! */
        else if (strchr(argv[oarg], '.') != NULL) {
            who_opts.host = argv[oarg];
            who_opts.flags.host = 1;
        } else 
            WHO_PARSE_SERROR(argv[0]);
    } else if (who_opts.flags.show_chan && 
            !(who_opts.flags.check_away || who_opts.gcos != NULL ||
                who_opts.host != NULL || who_opts.flags.check_umode ||
                who_opts.server != NULL || who_opts.user != NULL ||
                who_opts.nick != NULL || who_opts.ip != NULL ||
                who_opts.channel != NULL)) {
        /* if they asked for 'show channel' and then didn't specify anything
         * else, shot them an error. */
        if (oarg >= argc)
            WHO_PARSE_SERROR(argv[0]);
        else if (strchr(argv[oarg], '.') != NULL) {
            who_opts.host = argv[oarg];
            who_opts.flags.host = 1;
        } else {
            who_opts.nick = argv[oarg];
            who_opts.flags.nick = 1;
        }
    }

    return 1; /* success!  everything parsed okay. */
}

/* this checks an individual client to see if it matches everything necessary
 * from the who_opts structure.  it returns 1 if successful, and 0 otherwise.
 * showall should be set if it is okay to show invisible clients or other
 * information that should normally be hidden. */
static int who_check(client_t *cli, int showall) {

    if (INVIS(cli) && !showall)
        return 0; /* can't see them.  sneak sneak. */
    if (who_opts.flags.check_umode) {
        if ((who_opts.flags.umode &&
                !((cli->modes & who_opts.usermodes) == who_opts.usermodes)) ||
                (who_opts.flags.umode == 0 &&
                 (cli->modes & who_opts.usermodes) == who_opts.usermodes))
            return 0;
    }
    if (who_opts.flags.check_away) {
        if ((who_opts.flags.away && AWAYMSG(cli) == NULL) ||
                (who_opts.flags.away == 0 && AWAYMSG(cli) != NULL))
            return 0;
    }
    if (who_opts.server != NULL) {
        if ((who_opts.flags.server && cli->server != who_opts.server) ||
                (who_opts.flags.server == 0 &&
                 cli->server == who_opts.server))
            return 0;
    }
    if (who_opts.user != NULL) {
        if ((who_opts.flags.user && !match(who_opts.user, cli->user)) ||
                (who_opts.flags.user == 0 && match(who_opts.user, cli->user)))
            return 0;
    }
    if (who_opts.nick != NULL) {
        if ((who_opts.flags.nick && !match(who_opts.nick, cli->nick)) ||
                (who_opts.flags.nick == 0 && match(who_opts.nick, cli->nick)))
            return 0;
    }
    if (who_opts.host != NULL) {
        if (CAN_SEE_REAL_HOST(who_opts.issuer, cli)) {
            if ((who_opts.flags.host &&
                        !match(who_opts.host, cli->orighost)) ||
                    (who_opts.flags.host == 0 &&
                     match(who_opts.host, cli->orighost)))
                return 0;
        } else {
            if ((who_opts.flags.host && !match(who_opts.host, cli->host)) ||
                    (who_opts.flags.host == 0 &&
                     match(who_opts.host, cli->host)))
                return 0;
        }
    }
    if (who_opts.gcos != NULL) {
        if ((who_opts.flags.gcos && !match(who_opts.gcos, cli->info)) ||
                (who_opts.flags.gcos == 0 && match(who_opts.gcos, cli->info)))
            return 0;
    }
    if (who_opts.ip != NULL && CAN_SEE_REAL_HOST(who_opts.issuer, cli)) {
        if (
                (who_opts.flags.ip &&
                 (!match(who_opts.ip, cli->ip) &&
                  !ipmatch(who_opts.ip, cli->ip))) ||
                (who_opts.flags.ip == 0 &&
                 (match(who_opts.ip, cli->ip) ||
                  ipmatch(who_opts.ip, cli->ip))))
            return 0;
    }

    return 1; /* well, it passed, it must be a match. */
}

/* this finds the first visible channel on 'target' with regards to 'cli'. */
static char *who_first_visible(client_t *cli, client_t *target) {
    struct chanlink *clp;
    char *ret = "*";
    static char modname[CHANLEN + 2];
    int see;

    LIST_FOREACH(clp, &target->chans, lpcli) {
        see = can_can_see_channel(cli, clp->chan);
        if (onchannel(cli, clp->chan))
            ret = clp->chan->name;
        else if ((see = can_can_see_channel(cli, clp->chan)) ==
                CHANNEL_CHECK_OVERRIDE) {
            sprintf(modname, "%%%s", clp->chan->name);
            ret = modname;
            break;
        } else if (see < 0) {
            ret = clp->chan->name;
            break;
        }
    }

    return ret;
}

#define WHO_WHICH_HOST(cli, target) \
(CAN_SEE_REAL_HOST(cli, target) ? target->orighost : target->host)
#define WHO_WHICH_HOPS(cli, target) \
(CAN_SEE_SERVER(cli, target) ? target->hops : 0)
/* argv[1] = nick to /whois (or place to whois from if argc > 2)
 * argv[2] = nick to request data for */
CLIENT_COMMAND(who, 0, 0, 0) {
    int see = 0, showall = BPRIV(cli, priv_whoinvis);
    struct chanlink *clp;
    client_t *cp;
    char status[64];
    int max = IPRIV(cli, priv_wholimit);
    int shown = 0;
    int flood = COMMAND_WEIGHT_HIGH; /* flood penalty for the command */

    if (!MYCLIENT(cli))
        return COMMAND_WEIGHT_NONE; /* /WHOs don't get routed..but? */
    if (!who_parse_options(cli, argc - 1, argv + 1))
        return COMMAND_WEIGHT_MEDIUM; /* query was no good. */

    /* it parsed okay, now depending on what they asked for, reply differently.
     * if they asked for a channel, life is pretty easy, other queries are not
     * so simple. */
    if (who_opts.channel != NULL) {
        see = 0;
        /* if they can see the channel, we go through the members.  If they
         * can't see the channel, we do nothing here. */
        if (onchannel(cli, who_opts.channel)) {
            showall = 1; /* they're on the channel. */
            /* If they're in the channel treat this as less work, this is kind
             * of a kludge since most clients JOIN and then WHO (if they're
             * legitimate) and we don't want to penalize for that.  At any rate
             * while this may send a lot of data we don't spin on CPU looking
             * that data up, so it's not so bad. */
            flood = COMMAND_WEIGHT_MEDIUM;
        } else if ((see = can_can_see_channel(cli, who_opts.channel)) >= 0) {
            if (see)
                sendto_one(cli, RPL_FMT(cli, see), who_opts.channel->name);
            sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHO),
                    who_opts.channel->name);
            return flood;
        }
        LIST_FOREACH(clp, &who_opts.channel->users, lpchan) {
            cp = clp->cli;
            if (!who_check(cp, showall))
                continue; /* don't show them */
            sprintf(status, "%c%s%s", AWAYMSG(cp) != NULL ? 'G' : 'H',
                    OPER(cp) ? "*" : (INVIS(cp) && OPER(cli) ? "%" : ""),
                    chanmode_getprefixes(who_opts.channel, cp));
            sendto_one(cli, RPL_FMT(cli, RPL_WHOREPLY),
                    who_opts.channel->name, cp->user, WHO_WHICH_HOST(cli, cp),
                    cp->server->name, cp->nick, status,
                    WHO_WHICH_HOPS(cli, cp->server), cp->info);
        }
        sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHO), who_opts.channel->name);
        return flood;
    }

    /* if they want info on a specific client, provide it */
    if (who_opts.nick != NULL && strchr(who_opts.nick, '*') == NULL &&
            strchr(who_opts.nick, '?') == NULL) {

        /* Getting one nickname is trivial and won't send a lot of data. */
        flood = COMMAND_WEIGHT_LOW;

        cp = find_client(who_opts.nick);
        if (cp != NULL && who_check(cp, 1)) {
            sprintf(status, "%c%s", AWAYMSG(cp) != NULL ? 'G' : 'H',
                    OPER(cp) ? "*" : (INVIS(cp) && OPER(cli) ? "%" : ""));
            sendto_one(cli, RPL_FMT(cli, RPL_WHOREPLY),
                    who_opts.flags.show_chan ? who_first_visible(cli, cp) :
                    "*",  cp->user, WHO_WHICH_HOST(cli, cp), cp->server->name,
                    cp->nick, status, WHO_WHICH_HOPS(cp, cp->server),
                    cp->info);
        }
        sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHO), who_opts.nick);
        return flood;
    }

    /* if they want to search only the channels they're in (+M), do this */
    if (who_opts.flags.search_chan) {
        struct chanlink *chanclp;

        LIST_FOREACH(clp, &cli->chans, lpcli) {
            LIST_FOREACH(chanclp, &clp->chan->users, lpchan) {
                cp = chanclp->cli;
                if (!who_check(cp, 1))
                    continue;

                if (max && shown >= max) {
                    if (shown == max)
                        sendto_one(cli, RPL_FMT(cli, ERR_WHOLIMEXCEED), max);
                    max++;
                    break;
                } else {
                    sprintf(status, "%c%s%s", AWAYMSG(cp) != NULL ? 'G' : 'H',
                            OPER(cp) ? "*" : (INVIS(cp) &&
                                OPER(cli) ? "%" : ""),
                            chanmode_getprefixes(who_opts.channel, cp));
                    sendto_one(cli, RPL_FMT(cli, RPL_WHOREPLY),
                            clp->chan->name, cp->user, WHO_WHICH_HOST(cli, cp),
                            cp->server->name, cp->nick, status,
                            WHO_WHICH_HOPS(cli, cp->server), cp->info);
                    shown++;
                }
            }
        }
    } else {
        /* otherwise, do it over the whole network.  This is a high-intensity
         * search, so charge them severely for it. */
        flood = COMMAND_WEIGHT_EXTREME;
        LIST_FOREACH(cp, ircd.lists.clients, lp) {
            if (!CLIENT_REGISTERED(cp))
                continue; /* only registered clients ... */
            if (!who_check(cp, showall))
                continue; /* not a match */

            if (max && shown == max) {
                sendto_one(cli, RPL_FMT(cli, ERR_WHOLIMEXCEED), max);
                break;
            } else {
                sprintf(status, "%c%s", AWAYMSG(cp) != NULL ? 'G' : 'H',
                        OPER(cp) ? "*" : (INVIS(cp) && OPER(cli) ? "%" : ""));
                sendto_one(cli, RPL_FMT(cli, RPL_WHOREPLY),
                        who_opts.flags.show_chan ? who_first_visible(cli, cp) :
                        "*", cp->user, WHO_WHICH_HOST(cli, cp), 
                        cp->server->name, cp->nick, status,
                        WHO_WHICH_HOPS(cli, cp->server), cp->info);
                shown++;
            }
        }
    }
    sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHO),
            (who_opts.host != NULL ? who_opts.host :
             (who_opts.ip != NULL ? who_opts.ip :
              (who_opts.nick != NULL ? who_opts.nick :
               (who_opts.user != NULL ? who_opts.user :
                (who_opts.gcos != NULL ? who_opts.gcos :
                 (who_opts.server != NULL ? who_opts.server->name :
                  argv[0])))))));
    return flood;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
