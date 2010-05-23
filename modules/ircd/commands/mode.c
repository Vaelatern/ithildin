/*
 * mode.c: the MODE command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "commands/mode.h"

IDSTRING(rcsid, "$Id: mode.c 756 2006-06-25 18:56:59Z wd $");

/* maximum number of modes per line.  this limit is enforced on outbound sends,
 * and on commands from local users */
#define MAXMODES 6

MODULE_REGISTER("$Rev: 756 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/core
*/

/* called when a client is registered to set default modes */
HOOK_FUNCTION(mode_rc_hook);

MODULE_LOADER(mode) {
    int64_t i64 = MAXMODES;

    add_isupport("MODES", ISUPPORT_FL_INT, (char *)&i64);
    add_hook(ircd.events.register_client, mode_rc_hook);

#define RPL_UMODEIS 221
    CMSG("221", "%s");
#define RPL_CHANMODEIS 324
    CMSG("324", "%s %s %s");
#define RPL_CREATIONTIME 329
    CMSG("329", "%s %lu");
#define ERR_UNKNOWNMODES 672
    CMSG("672", "%s :%s");
#define ERR_CANNOTSETMODES 673
    CMSG("673", "%s :%s");
#define ERR_USERSDONTMATCH 502
    CMSG("502", ":Can't change mode for other users");

    return 1;
}
MODULE_UNLOADER(mode) {

    del_isupport(find_isupport("MODES"));
    remove_hook(ircd.events.register_client, mode_rc_hook);

    DMSG(RPL_UMODEIS);
    DMSG(RPL_CHANMODEIS);
    DMSG(RPL_CREATIONTIME);
    DMSG(ERR_UNKNOWNMODES);
    DMSG(ERR_CANNOTSETMODES);
    DMSG(ERR_USERSDONTMATCH);
}

/* this command is a bit convoluted, there are two potential paths:
 * argv[1] == user:
 *   argv[2] ?= modes to set
 * argv[1] == channel:
 *   argv[2] ?= channel ts (for remote clients)
 *    argv[3] == modes to set
 *    argv[4...n] == arguments to those modes
 *   argv[2] ?= modes to set (for local clients)
 *    argv[3...n] == arguments to those modes */
CLIENT_COMMAND(mode, 1, 0, COMMAND_FL_REGISTERED) {
    channel_t *chan;
    client_t *cp;

    if (check_channame(argv[1])) {
        chan = find_channel(argv[1]);
        if (chan == NULL) {
            if (MYCLIENT(cli))
                sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHCHANNEL), argv[1]);
            return COMMAND_WEIGHT_NONE;
        }
        if (!MYCLIENT(cli)) {
            /* check TSMODE support here */
            if (SERVER_SUPPORTS(sptr, PROTOCOL_SFL_TSMODE))
                channel_mode(cli, NULL, chan, str_conv_int(argv[2], 0),
                        argc - 3, &argv[3], 1);
            else
                channel_mode(cli, NULL, chan, chan->created, argc - 2,
                        &argv[2], 1);
        } else {
            return channel_mode(cli, NULL, chan, chan->created, argc - 2,
                    &argv[2], 1);
        }
    } else if ((cp = find_client(argv[1])) != NULL) {
        user_mode(cli, cp, argc -2, argv + 2, 1);
        return COMMAND_WEIGHT_LOW;
    }
    else
        sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHNICK), argv[1]);

    return COMMAND_WEIGHT_NONE;
}

/* this is much like the client command..
 * argv[1] == user:
 *   argv[2] ?= modes to set
 * argv[1] == channel:
 *   argv[2] == channel ts
 *   argv[3] == modes to set
 *   argv[4...n] == arguments to those modes */
SERVER_COMMAND(mode, 2, 0, 0) {
    channel_t *chan;

    if (check_channame(argv[1])) {
        chan = find_channel(argv[1]);
        if (chan == NULL)
            return ERR_NOSUCHCHANNEL;
        /* It's a valid channel.  Now see if the server supports TSMODE or
         * sends modes the old-fashioned way. */
        if (SERVER_SUPPORTS(sptr, PROTOCOL_SFL_TSMODE))
            channel_mode(NULL, srv, chan, str_conv_int(argv[2], 0),
                    argc - 3, &argv[3], 1);
        else
            channel_mode(NULL, srv, chan, chan->created, argc - 2,
                    &argv[2], 1);
    } else if (SERVER_MASTER(srv)) {
        /* let master servers change users' modes. */
        client_t *cli = find_client(argv[1]);
        if (cli != NULL)
            user_mode(NULL, cli, argc - 2, argv + 2, 1);
    }
    return 0;
}

void user_mode(client_t *cli, client_t *on, int argc, char **argv, int out) {
    unsigned char *modes = (unsigned char *)argv[0];
    uint64_t oldmode;
    int plus = 1; /* default action is + */
    int argused = 0, oarg = 1;
    unsigned char unknown[512], *u = unknown;
    unsigned char noperm[512], *n = noperm;
    unsigned char result[512];

    if (argc < 1) {
        if (MYCLIENT(on))
            sendto_one(on, RPL_FMT(on, RPL_UMODEIS),
                    usermode_getstr(on->modes, 0));
        return;
    }

    /* if cli != on and cli is not a master client, return */
    if (cli != NULL && cli != on && !CLIENT_MASTER(cli)) {
        sendto_one(cli, RPL_FMT(cli, ERR_USERSDONTMATCH));
        return;
    }

    oldmode = on->modes;
    while (*modes) {
        switch (*modes) {
            case '+':
                plus = 1;
                break;
            case '-':
                plus = 0;
                break;
            default:
                if (argused)
                    oarg++;
                argused = 0;

                if (plus && !usermode_set(*modes, cli, on,
                            (oarg >= argc ? NULL : argv[oarg]), &argused)) {
                    if (ircd.umodes.modes[*modes].avail)
                        *u++ = *modes;
                    else
                        *n++ = *modes;
                } else if (!plus && !usermode_unset(*modes, cli, on,
                            (oarg >= argc ? NULL : argv[oarg]), &argused)) {
                    if (ircd.umodes.modes[*modes].avail)
                        *u++ = *modes;
                    else
                        *n++ = *modes;
                }
                break;
        }
        modes++;
    }
    *u = *n = '\0';

    if (MYCLIENT(on)) {
        /* now send numerics/etc */
        if (*unknown != '\0')
            sendto_one(cli, RPL_FMT(cli, ERR_UNKNOWNMODES), unknown,
                    (*(unknown + 1) != '\0' ?
                     "are unknown mode characters to me" :
                     "is an unknown mode character to me"));
        if (*noperm != '\0')
            sendto_one(cli, RPL_FMT(cli, ERR_CANNOTSETMODES), noperm,
                    (*(noperm + 1) != '\0' ?
                     "you may not modify these modes" :
                     "you may not modify this mode"));

        usermode_diff(oldmode, on->modes, result, 0);
        if (*result == '\0')
            return; /* they didn't actually set anything... */
        if (cli != NULL)
            sendto_one_from(on, cli, NULL, "MODE", ":%s", result);
        else
            sendto_one_from(on, on, NULL, "MODE", ":%s", result);
    }

    /* propogate along */
    if (out) {
        usermode_diff(oldmode, on->modes, result, 1);
        if (*result == '\0')
            return;
        if (cli != NULL)
            sendto_serv_butone(sptr, cli, NULL, on->nick, "MODE", ":%s",
                    result);
        else
            sendto_serv_butone(sptr, on, NULL, on->nick, "MODE", ":%s",
                    result);
    }
        
    return;
}

HOOK_FUNCTION(mode_rc_hook) {
    client_t *cli = (client_t *)data;
    char *fakeargv[2];

    if (MYCLIENT(cli)) {
        fakeargv[0] = (cli->conn ? cli->conn->cls->default_mode : "+");
        fakeargv[1] = NULL;
        user_mode(cli, cli, 1, fakeargv, 1);
    }

    return NULL;
}

#define SEND_MODES_MAYBE(spare) {                                             \
    if (cnt == MAXMODES || (spare && cnt)) {                                  \
        *r = '\0';                                                            \
        sendto_channel_local(chan, NULL,                                      \
                (cli != NULL ? cli_server_uplink(cli) : srv), "MODE",         \
                "%s %s", result, rbuf);                                       \
        sendto_serv_pflag_butone(PROTOCOL_SFL_TSMODE, false, sptr, cli,       \
                    srv, chan->name, "MODE", "%s%s %d", result, rbuf,         \
                    chan->created);                                           \
        r = result + 1;                                                       \
        rblen = 0;                                                            \
        *rb = '\0';                                                           \
        cnt = 0;                                                              \
    }                                                                         \
}
/* this is a lot like my mode changer from bahamut 1.x.  hopefully it'll be a
 * bit cleaner/more readable, though.  Oh, and we now do the logic of ts
 * changes completely in this function.  lovely.  argc is the count only of the
 * mode changes and any arguments, and argv[0] is the mode changes if argc is
 * not zero. */
int channel_mode(client_t *cli, server_t *srv, channel_t *chan, time_t ts,
        int argc, char **argv, int out) {
    int oarg = 1;
    int plus = 1;
    int res;
    int optused;
    int mset = 0;
    unsigned char *modes = (unsigned char *)argv[0];
    unsigned char unknown[512], *u = unknown;
    unsigned char noperm[512], *n = noperm;
    unsigned char result[64], *r = result; /* these two hold the result change
                                              string and the result change
                                              arguments */
#define RBUF_SIZE 320
    char rbuf[RBUF_SIZE], *rb = rbuf;
    int rblen = 0;
    bool changeok = true, keepours = true;

    if (cli != NULL && MYCLIENT(cli) && argc == 0) {
        const char **mgunk = chanmode_getmodes(chan);

        res = can_can_see_channel(cli, chan);
        /* hmm.. if they can't see the channel let's not give them any data.
         * ooh, ahh. */
        if (res < 0) {
            sendto_one(cli, RPL_FMT(cli, RPL_CHANMODEIS), chan->name, mgunk[0],
                    (onchannel(cli, chan) || res == CHANNEL_CHECK_OVERRIDE ?
                     mgunk[1] : ""));
            sendto_one(cli, RPL_FMT(cli, RPL_CREATIONTIME), chan->name,
                    chan->created);
            return COMMAND_WEIGHT_LOW;
        } else
            /* well, a clever person should be able to tell through at most two
             * queries whether or not this is a lie (because of case
             * insensitivity).  still..  if they can't see the channel they
             * shouldn't get any report on it, and this might fool some. */
            sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHCHANNEL), chan->name);
        return COMMAND_WEIGHT_NONE;
    } else if (argc == 0)
        return COMMAND_WEIGHT_NONE; /* nothing else should be allowed to do
                                       this. */

    /*
     * Check timestamp.  From servers which do not have the TS flag in their
     * protocol we ACCEPT the changes and set the channel's TS to 0 (this is
     * a bad hack to support older servers, not reasonable behavior).
     * In the case where TS is properly supported from the sending server we
     * only accept modes if ts is <= to our own.
     *
     * If the sender's TS for the channel is LESS than ours we remove every
     * mode we have set on the channel, treating them all as invalid.  This
     * case should only *ever* occur during synchronization.  We do not send
     * the mode changes down in the opposite direction because we will send
     * this command, with the correct TS, down the wire and all other TS
     * servers will do the same thing.
     *
     * Similarly if we ignore their changes (ts > ours) we simply return,
     * believing that a TS server will properly remove its modes.
     *
     * This is perhaps an overly trusting maneuever and, should this become
     * an issue, the code #if 0'd out below should suffice to reverse any
     * changes we got. :)
     */

    if ((cli != NULL && !MYCLIENT(cli)) || (srv != NULL && srv != ircd.me)) {
        /* We need the uplink for either the server or client in order to
         * verify flags. */
        server_t *sp = (srv != NULL ? srv_server_uplink(srv) : cli_server_uplink(cli));

        if (!SERVER_SUPPORTS(sp, PROTOCOL_SFL_TS)) {
            /* server does not have TS support, set ts to 0, but we do not
             * remove our own modes (because we believe the server will
             * accept our modes as stupidly as we accepted theirs... */
            if (srv != NULL && !SERVER_MASTER(srv)) {
                log_debug("server %s has no TS support, setting TS for %s to 0!",
                        sp->name, chan->name);
                ts = 0;
                chan->created = 0; /* normally set when we reject our modes, but
                                      we don't reject our modes in this case..  */
            } else
                /* For master servers without TS protocol support just
                 * default to giving them what we think is the right TS.
                 * Why do this?  Because it fits in with the whole 'master'
                 * concept.  We're going to take these modes no matter what.
                 * Why break TS over it? */
                ts = chan->created;
        } else {
            if (ts > chan->created)
                changeok = false; /* no changes! */
            else if (ts < chan->created)
                keepours = false;
        }
    }

    if (!changeok) {
        log_debug("rejecting changes to channel %s (ours=%d, theirs=%d",
                chan->name, chan->created, ts);
#if 0
        /* invert their modes and send them right back. */
        *rb = '\0';
        while (*modes) {
            switch (*modes) {
                case '+':
                    *r++ = '-';
                    break;
                case '-':
                    *r++ = '+';
                    break;
                default:
                    *r++ = *modes;
                    break;
            }
            modes++;
        }
        *r = '\0';

        if (result[1] == '\0' && (result[0] == '+' || result[0] == '-'))
            return; /* an empty mode string.  no need to send back anything. */

        while (argc > oarg)
            rblen += sprintf(rb + rblen, " %s", argv[oarg++]);

        sendto_serv_from((cli != NULL ? cli_server_uplink(cli) : srv), NULL,
                ircd.me, chan->name, "MODE", "%d %s%s", chan->created, result,
                rbuf);
#endif
        return COMMAND_WEIGHT_NONE; /* okay, now, really, we're done! */
    } else if (!keepours) {
        /* this is a rather complex case, we basically have to wade through
         * our modes and remove them as we go.  First we remove channel
         * modes, then user prefixes. 
         *
         * NB: We send reverted modes to local clients and non-ts servers
         * only.  Servers which are TS will get the updated TS and revert
         * their modes accordingly on their own.  This helps to reduce spam
         * from changes of this nature. */
        unsigned char *s;
        int cnt = 0;

        result[0] = '-';
        r = result + 1;
        rblen = 0;

        log_debug("reverting our modes for channel %s (ours=%d, theirs=%d",
                chan->name, chan->created, ts);
        chan->created = ts;

        s = ircd.cmodes.avail;
        while (*s) {
            void *state = NULL; /* for chanmode_query */

            if (ircd.cmodes.modes[*s].flags & CHANMODE_FL_PREFIX) {
                /* a prefix-type mode, walk the channel users list and unset
                 * anyone with this prefix */
                struct chanlink *clp;
                LIST_FOREACH(clp, &chan->users, lpchan) {
                    if (chanlink_ismode(clp, *s)) {
                        /* unset them, and send the mode too. */
                        clp->flags &= ~ircd.cmodes.modes[*s].umask;
                        *r++ = *s;
                        rblen += sprintf(rb + rblen, "%s ", clp->cli->nick);
                        cnt++;
                        SEND_MODES_MAYBE(0);
                    }
                }
            } else {
                /* We must now query against each mode and burn it off.  We
                 * call chanmode_query and then use chanmode_unset on the
                 * data it gives us back as well.  This replaces some mostly
                 * unhelpful chanmode_reset code that really just got in the
                 * way.
                 */
                optused = RBUF_SIZE - rblen - 2;
                while (!chanmode_query(*s, chan, rb + rblen, &optused, &state)) {
                    int spam; /* needed for the ignored optused in
                                 chanmode_unset */

                    if (optused < 0) {
                        /* no buffer space?  force a send. */
                        SEND_MODES_MAYBE(1);
                        continue;
                    }
                    /* k, it worked out for us, let's go ahead and call
                     * chanmode_unset here.. 
                     * NOTE: this is a weird setup.  we assume quietly that
                     * state does not depend on the modes staying the same
                     * throughout it.  for list modes (where this is
                     * actually important) what we expect is that we will
                     * get the head of the list as an argument, and then the
                     * state will have been set to the *next item in the
                     * list* so we can delete this item. */
                    chanmode_unset(*s, NULL, chan, rb + rblen, &spam);

                    *r++ = *s;
                    cnt++;
                    if (optused) {
                        /* if they used an option, append a space on the end
                         * for us. */
                        rblen += optused;
                        rbuf[rblen++] = ' ';
                        rbuf[rblen] = '\0';
                    }
                    SEND_MODES_MAYBE(0);
                }
            }
            s++; /* and onto the next */
        }
        SEND_MODES_MAYBE(1); /* send off any extras */
    }

    /* reset stuff from above. */
    r = result;
    rb = rbuf;
    rblen = 0;
    *r++ = '+';
    while (*modes) {
        if (cli != NULL && MYCLIENT(cli) && mset >= MAXMODES)
            break; /* no more than MAXMODES for local clients */

        switch (*modes) {
            case '+':
                if (plus)
                    break; /* no change, ignore extraneous + */
                /* stick it in the modestring */
                if (*(r - 1) == '-')
                    *(r - 1) = '+';
                else
                    *r++ = '+';
                plus = 1;
                break;
            case '-':
                if (!plus)
                    break; /* no change, ignore extraneous + */
                /* stick it in the modestring */
                if (*(r - 1) == '+')
                    *(r - 1) = '-';
                else
                    *r++ = '-';
                plus = 0;
                break;
            default:
                if (argc > oarg && (RBUF_SIZE - rblen) <= strlen(argv[oarg])) {
                    modes++;
                    break; /* skip modes when we're in danger of overflowing
                              rbuf.  XXX: this can be done better :/ */
                }
                if (ircd.cmodes.modes[*modes].avail) {
                    *u++ = *modes++;
                    continue;
                }

                /* XXX: some consumers (like samode from old bahamut servers)
                 * like to send client modes when the client isn't even on the
                 * channel.  that's really fairly broken. :/  for now if the
                 * client isn't local pass it as NULL when doing the handlers.
                 * This, by extension, is really broken and should be FIXED
                 * later, not left this way! */
                optused = 0;
                if (plus)
                    res = chanmode_set(*modes,
                            (cli != NULL && MYCLIENT(cli) ? cli : NULL),
                            chan, (argc > oarg ?  argv[oarg] : NULL),
                            &optused);
                else
                    res = chanmode_unset(*modes,
                            (cli != NULL && MYCLIENT(cli) ? cli : NULL),
                            chan, (argc > oarg ?  argv[oarg] : NULL),
                            &optused);
                if (res == CHANMODE_OK) {
                    /* success! */
                    *r++ = *modes;
                    if (optused)
                        rblen += sprintf(&rbuf[rblen], " %s", argv[oarg]);
                    mset++;
                } else if (res > 0 && cli != NULL)
                    /* send off an error numeric.  The numeric should only take
                     * one argument (the channel).  If it takes more, the
                     * mode function should do the sending! */
                    sendto_one(cli, RPL_FMT(cli, res), chan->name);
                else if (res == CHANMODE_NONEX)
                    *u++ = *modes;
                else if (res == CHANMODE_FAIL) 
                    *n++ = *modes;

                if (optused)
                    oarg++; /* even if it failed, the argument is gone */
        }
        modes++;
    }
    *u = *r = *n = '\0';
    rbuf[rblen] = '\0';

    if (cli != NULL && MYCLIENT(cli)) {
        if (*unknown != '\0')
            sendto_one(cli, RPL_FMT(cli, ERR_UNKNOWNMODES), unknown,
                    (*(unknown + 1) != '\0' ?
                     "are unknown mode characters to me" :
                     "is an unknown mode character to me"));
        if (*noperm != '\0')
            sendto_one(cli, RPL_FMT(cli, ERR_CANNOTSETMODES), noperm,
                    (*(noperm + 1) != '\0' ?
                     "you may not modify these modes" :
                     "you may not modify this mode"));
    }

    /* propogate along if we did anything */
    if (result[1] != '\0') {
        r--;
        if (*r == '+' || *r == '-')
            *r = '\0';
        sendto_channel_local(chan, cli, srv, "MODE", "%s%s", result, rbuf);
        /* if out is 1, send it down the pipe, otherwise don't (send might be 0
         * for stuff like SJOIN where modes are sent some other way) */
        if (out) {
            sendto_serv_pflag_butone(PROTOCOL_SFL_TSMODE, true, sptr, cli, srv,
                    chan->name, "MODE", "%d %s%s", chan->created, result,
                    rbuf);
            sendto_serv_pflag_butone(PROTOCOL_SFL_TSMODE, false, sptr, cli,
                    srv, chan->name, "MODE", "%s%s %d", result, rbuf,
                    chan->created);
        }
    }
    return COMMAND_WEIGHT_HIGH;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
