/*
 * nick.c: the NICK command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"
#include "commands/mode.h"

IDSTRING(rcsid, "$Id: nick.c 758 2006-07-07 04:14:44Z wd $");

MODULE_REGISTER("$Rev: 758 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/commands/mode
*/

static client_t *check_collision(client_t *, client_t *, bool);

MODULE_LOADER(nick) {

#define ERR_NONICKNAMEGIVEN 431
    CMSG("431", ":No nickname given");
#define ERR_NICKNAMEINUSE 433
    CMSG("433", "%s :Nickname is already in use.");
#define ERR_NICKCOLLISION 436
    CMSG("436", "%s :Nickname collision KILL");

    return 1;
}
MODULE_UNLOADER(nick) {

    DMSG(ERR_NONICKNAMEGIVEN);
    DMSG(ERR_NICKNAMEINUSE);
    DMSG(ERR_NICKCOLLISION);
}

/*
 * argv[1] == nickname
 * argv[2] == ts (remote users only)
 */
CLIENT_COMMAND(nick, 1, 2, COMMAND_FL_UNREGISTERED|COMMAND_FL_REGISTERED) {
    char first[2] = {'\0', '\0'};
    client_t *cp;
    int casechng = 0; /* set to 1 if it's just a case change */
    int changeok = 1; /* set if we can change nicks. */
    struct chanlink *clp;
    time_t ts = me.now;

    if (MYCLIENT(cli)) {
        /* check a few things right away */
        if (*argv[1] == '\0') { /* is this possible? maybe */
            sendto_one(cli, RPL_FMT(cli, ERR_NONICKNAMEGIVEN));
            return COMMAND_WEIGHT_NONE;
        }
    } else if (argc > 2) /* set ts if given */
        ts = str_conv_int(argv[2], 0);

    first[0] = *argv[1];
    if (!check_nickname(argv[1]) || strlen(argv[1]) > ircd.limits.nicklen) {
        if (MYCLIENT(cli))
            sendto_one(cli, RPL_FMT(cli, ERR_ERRONEOUSNICKNAME), argv[1],
                    "Erroneous nickname (invalid characters)", "*");
        else {
            log_warn("bad nickname from %s: %s", cli->server->name, argv[0]);
            /* send a kill back at them */
            sendto_serv_from(sptr, NULL, ircd.me, argv[1], "KILL",
                    ":%s (bad nickname)", ircd.me->name);
        }
        return COMMAND_WEIGHT_NONE; /* bad nicks are not accepted */
    }

    /* shorten the nick to the maximum length */
    argv[1][ircd.limits.nicklen] = '\0';

    casechng = !istrcmp(ircd.maps.nick, cli->nick, argv[1]);
    /* check to see if the nickname is in use, and isn't just a case change. */
    if (!casechng && (cp = find_client_any(argv[1])) != NULL) {
        /* if its our client, just tell them the nickname is already in use */
        if (MYCLIENT(cli)) {
            sendto_one(cli, RPL_FMT(cli, ERR_NICKNAMEINUSE), argv[1]);
            return COMMAND_WEIGHT_NONE;
        }
        /* check for collision.  'ours' (the good client) is cp, 'theirs' (the
         * suspect client) is cli.  If cli doesn't come back it's over.
         * Why this isn't a race condition on cli:
         * If cli is ours we took it above, so we don't need to worry about
         * our memory structure going away.  If cli isn't ours we do no more
         * processing down the command handling chain that involves
         * modifying or even reading from cli, so even if cli goes away here
         * it's safe.
         */
        if (check_collision(cp, cli, false) != cli)
            return COMMAND_WEIGHT_NONE;
    }

    /* if the client is unregistered, just copy their nick in, er, and add
     * them to the client hash table too */
    if (!CLIENT_REGISTERED(cli)) {
        changeok = can_can_nick_client(cli, argv[1]);
        if (changeok > 0)
            sendto_one(cli, RPL_FMT(cli, changeok), argv[1]);
        else if (changeok < 0) {
            client_change_nick(cli, argv[1]);
            /* if the username has been filled in then assume the USER command
             * was called as well. */
            if (*cli->user != '\0')
                return register_client(cli);
        }

        return COMMAND_WEIGHT_NONE; /* nothing below here applies to
                                       unregistered clients */
    }

    /* otherwise, the client is already online, so attempt a nick change, we
     * know the nick is not in use from above, so just do the swap and
     * propogate the change if they're remote.  if they're local, do a some
     * access checks to make sure it's okay. */

    if (casechng && !strcmp(cli->nick, argv[1]))
        return COMMAND_WEIGHT_LOW; /* this actually is not a case change. */

    if (MYCLIENT(cli)) {
        /* see if they can change nicks on all their channels. */
        LIST_FOREACH(clp, &cli->chans, lpcli) {
            changeok = can_can_nick_channel(cli, clp->chan, argv[1]);
            if (changeok < 0)
                continue; /* okay.. */
            if (changeok >= 0) {
                if (changeok == ERR_BANONCHAN ||
                        changeok == ERR_BANNICKCHANGE)
                    sendto_one(cli, RPL_FMT(cli, changeok), argv[1],
                            clp->chan->name);
                return COMMAND_WEIGHT_MEDIUM; /* uh uh. */
            }
        }
        changeok = can_can_nick_client(cli, argv[1]);
        if (changeok >= 0) {
            if (changeok > 0)
                sendto_one(cli, RPL_FMT(cli, changeok), argv[1]);
            return COMMAND_WEIGHT_MEDIUM;
        }
    }

    /* See the comment in the server command that goes with this code for an
     * explanation of why we do this. */
    if (SERVER_SUPPORTS(sptr, PROTOCOL_SFL_TS))
        cli->ts = ts;
    else
        cli->ts = 0;
    /* send them a message about the nick change, even if they're not in
     * a channel, sendto_common_channels ensures they'll get the message */
    sendto_common_channels(cli, NULL, "NICK", ":%s", argv[1]);
    sendto_serv_butone(sptr, cli, NULL, NULL, "NICK", "%s :%d",
            argv[1], cli->ts);
    client_change_nick(cli, argv[1]);

    return COMMAND_WEIGHT_MEDIUM;
}

/*
 * server nick command.  considerably more complex:
 * different server protocols have different ideas about what belongs in a NICK
 * line.  We do our best to account for them here (sigh..).  We examine sptr's
 * protocol and construct our arguments based on that.
 */
SERVER_COMMAND(nick, 7, 10, 0) {
    client_t *cp, *cp2; /* the client being created. and a temp var*/
    char first[2] = {'\0', '\0'};
    char *nick, *hops, *ts, *umodes, *user, *host, *server, *ipaddr, *gcos;

    nick = hops = ts = umodes = user = host = server = ipaddr = gcos = NULL;
    if (sptr->conn != NULL && !strcmp(sptr->conn->proto->name, "bahamut14")) {
        /* bahamut 1.4 behavior:
         * arglist: nick hops ts umodes username hostname server sid ip gcos */
        nick = argv[1];
        hops = argv[2];
        ts = argv[3];
        umodes = argv[4];
        user = argv[5];
        host = argv[6];
        server = argv[7];
        ipaddr = argv[9];
        gcos = argv[10];
    } else if (sptr->conn != NULL && !strcmp(sptr->conn->proto->name,
                "dreamforge")) {
        /* dreamforge format:
         * arglist: nick hops ts username hostname servername sid gcos */
        nick = argv[1];
        hops = argv[2];
        ts = argv[3];
        user = argv[4];
        host = argv[5];
        server = argv[6];
        gcos = argv[8];
    } else {
        /* default format:
         * arglist: nick hops ts username hostname servername gcos */
        nick = argv[1];
        hops = argv[2];
        ts = argv[3];
        user = argv[4];
        host = argv[5];
        server = argv[6];
        gcos = argv[7];
    }

    cp = create_client(NULL); /* create a new, remote client */
    
    /* do rudamentary checks first */
    if (server == NULL || (cp->server = find_server(server)) == NULL) {
        log_warn("attempt to introduce client %s from non-existant server %s "
                "ignored!", nick, server);
        destroy_client(cp, "");
        return 0;
    }

    first[0] = *nick;
    if (!istr_okay(ircd.maps.nick_first, first) || !istr_okay(ircd.maps.nick,
            nick) || strlen(nick) > ircd.limits.nicklen) {
        log_warn("bad nickname from %s: %s", server, nick);
        /* send a kill back at them */
        sendto_serv_from(sptr, NULL, ircd.me, argv[1], "KILL",
                ":%s (bad nickname)", ircd.me->name);
        destroy_client(cp, "");
        return 0; /* bad nicks are not accepted */
    }

    /* fill it in */
    strncpy(cp->nick, nick, ircd.limits.nicklen);
    strncpy(cp->user, user, USERLEN);
    strncpy(cp->host, host, HOSTLEN);
    if (ipaddr != NULL) {
        struct in_addr ina;
        unsigned long ip;
        ip = htonl(strtoul(ipaddr, NULL, 0));
        memcpy(&ina, &ip, 4);
        strcpy(cp->ip, inet_ntoa(ina));
    } else
        strcpy(cp->ip, "0.0.0.0");
    strncpy(cp->info, gcos, GCOSLEN);
    cp->hops = str_conv_int(hops, 1);
    cp->signon = str_conv_int(ts, 0);
    /* Do not give the client ts if it comes from a non-TS server!
     * 
     * The implication of this is that if a hub is non-TS then none of the
     * leaves behind it have trustworthy TS either.  This leaves a bad
     * situation with regards to collisions, but is really the only way to deal
     * with untrusted hybrid networks. */
    if (SERVER_SUPPORTS(sptr, PROTOCOL_SFL_TS))
        cp->ts = cp->signon;
    else
        cp->ts = 0;

    /* now, error check! */
    if ((cp2 = find_client_any(cp->nick)) != NULL) {
        /* check to see what to collide */
        if (check_collision(cp2, cp, true) != cp)
            return 0;
    }

    /* set their user modes the right way.  we do this here so that mode
     * handlers don't get called on a nickname about to go up in smoke, instead
     * of above where the client could get killed out from under us before
     * passing registration. */
    if (umodes != NULL) {
        char *fargv[2] = {umodes, NULL};
        user_mode(cp, cp, 1, fargv, 0);
    }
    /* add them to the network.  yay */
    register_client(cp);

    return 0;
}

/* little function to detect/handle nick collisions, this is done in a few
 * places, so we coalesce into one function, 'ours' is the client we knew of,
 * theirs is the client that might be suspect.  the outcome isn't always
 * favorable to "ours" though.  the function returns the client who 'won'
 * the check, in the case of equal ts nobody 'wins' and the result is a NULL
 * return. */
static client_t *
check_collision(client_t *known, client_t *unknown, bool new) {

    /* This condition is extremely incorrect for what should be obvious
     * reasons. */
    assert(known != unknown);

    /* known is the only one which might be a local connection.  if it is on
     * our server and is unregistered then drop it no matter what. */
    if (MYCLIENT(known) && !CLIENT_REGISTERED(known)) {
        destroy_client(known, "Overridden");
        return unknown; /* they win */
    }

    /* Check for 'no ts' conditions.  If either ts is 0, or if both timestamps
     * are the same, we collide. */
    if (known->ts == 0 || unknown->ts == 0 || known->ts == unknown->ts) {
        /* this is the case where we're killing both clients.  propogate a
         * kill in both directions, and dump the client structures locally.
         * this is the worst case scenario */
        sendto_one(known, RPL_FMT(known, ERR_NICKCOLLISION), unknown->nick);
        sendto_one(unknown, RPL_FMT(unknown, ERR_NICKCOLLISION), known->nick);
        if (!new)
            /* send out the kill for the old nick too */
            sendto_serv_butone(NULL, NULL, ircd.me, unknown->nick, "KILL",
                    ":%s (Nick collision)", ircd.me->name);
        sendto_serv_butone(NULL, NULL, ircd.me, known->nick, "KILL",
                ":%s (Nick collision)", ircd.me->name);
        known->flags |= IRCD_CLIENT_KILLED;
        unknown->flags |= IRCD_CLIENT_KILLED;
        destroy_client(known, "Nick collision");
        destroy_client(unknown, "Nick collision");
        return NULL;
    }

    /* NB: we don't observe the 'samehost' nonsense that other servers do.
     * why?  because it obfuscates the code and adds a weird edge-case that is
     * not all that helpful. */

    /* If the unknown client is newer then we do one of two things: */
    if (known->ts < unknown->ts) {
        /* if this is a nick change we send a kill back for that user, if this
         * is a new user we simply dump the structure locally and then return
         * without any output (assuming it will be killed further down as
         * whatever message *we* have causing it to be invalid must not have
         * reached its destination.
         *
         * XXX: This seems like a really bad implementation to me.  On the
         * other hand I don't see any reasonable way to repair this short of
         * issuing a double kill.  On a network with non-TS servers it is
         * critical that users not be given a timestamp value other than 0 if
         * they come from non-TS systems (think mixed df/hybrid nets, perhaps).
         * the 'ts' field passed by non-TS servers should only be used as a
         * signon time, not as a client timestamp. */
        if (!new) {
            sendto_one(unknown, RPL_FMT(unknown, ERR_NICKCOLLISION),
                    known->nick);
            /* sptr is going to be the server sending the unknown client */
            sendto_serv_butone(sptr, NULL, ircd.me, unknown->nick, "KILL",
                    ":%s (Nick collision)", ircd.me->name);
        }
        unknown->flags |= IRCD_CLIENT_KILLED;
        destroy_client(unknown, "Nick collision");

        return known;
    } else {
        /* the inbound client is actually older, we need to shoot a kill
         * out that way in preparation for the forthcoming message. */
        known->flags |= IRCD_CLIENT_KILLED;
        sendto_one(known, RPL_FMT(known, ERR_NICKCOLLISION), unknown->nick);
        sendto_serv_butone(sptr, NULL, ircd.me, known->nick, "KILL",
                ":%s (Nick collision)", ircd.me->name);
        destroy_client(known, "Nick collision");

        return unknown;
    }
}

