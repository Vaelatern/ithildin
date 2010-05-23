/*
 * sjoin.c: the SJOIN command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "commands/mode.h"

IDSTRING(rcsid, "$Id: sjoin.c 757 2006-07-05 02:17:59Z wd $");

MODULE_REGISTER("$Rev: 757 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/commands/mode
*/

/* SJOIN: (client)
 * easy.  channel ts (which is basically ignored) and channel, this should
 * only come for channels which previously exist.  new channels should be
 * created by server-sent SJOINs. */
CLIENT_COMMAND(sjoin, 2, 2, 0) {
    channel_t *chan;

    if (MYCLIENT(cli))
        return COMMAND_WEIGHT_NONE; /* silent failure */

    chan = find_channel(argv[2]);
    if (chan == NULL) {
        log_warn("got client SJOIN for an empty channel %s", argv[2]);
        chan = create_channel(argv[2]);
        chan->created = str_conv_int(argv[1], 0);
    }

    /* add them to the channel and ship the SJOIN along */
    add_to_channel(cli, chan, true);
    sendto_channel_local(chan, cli, NULL, "JOIN", NULL);
    sendto_serv_pflag_butone(PROTOCOL_SFL_SJOIN, true, cli->server, cli, NULL,
            NULL, "SJOIN", "%d %s", chan->created, chan->name);
    sendto_serv_pflag_butone(PROTOCOL_SFL_SJOIN, false, cli->server, cli, NULL,
            NULL, "JOIN", "%s", chan->name);

    return COMMAND_WEIGHT_NONE;
}

/* SJOIN: (server)
 * this gets complicated.  the first argument (argv[1]) will be the timestamp
 * of the channel.  the next is the channel name, then modes and arguments.
 * the last is a space separated list of people joining whose statuses are
 * demarked with whatever prefixes are available.
 */ 
SERVER_COMMAND(sjoin, 4, 5, 0) {
    channel_t *chan;
    char *client, *prefix;
    char *buf = argv[argc - 1];
    char realjoiners[512];
    client_t *cp;
    time_t ts = str_conv_int(argv[1], 0);
    bool changeok = true;
    int i;
    int mset = 0;
    int mlen = 0;
    char modes[320];
    char modebuf[320];

    mlen = 0;
    /* make sure to set this incase we never actually touch the modes below,
     * this prevents us from sending out goofy SJOIn data */
    *modes = *modebuf = '\0';
    chan = find_channel(argv[2]);

    /* if we're creating the channel, trust their timestamp */
    if (chan == NULL) {
        chan = create_channel(argv[2]);
        chan->created = ts;
    } 

    /* Check TS before running channel_mode (which will reset TS in a
     * variety of conditions).  All we need to know is whether or not to
     * accept ops down below, channel_mode does the rest of the logic... */
    if (ts > chan->created)
        changeok = false;

    /* handle mode changes.  we basically dump off the stuff to the mode
     * command handler.  We subtract four from argc (argv[0] is cmd,
     * argv[1] is ts, argv[2] is channel, argv[argc - 1] is other sjoin
     * arguments. */
    channel_mode(NULL, srv, chan, ts, (argc - 4), argv + 3, 0);

    /* now parse the list of joiners, and propogate our SJOIN out to others */
    *realjoiners = '\0';
    while ((client = strsep(&buf, " ")) != NULL) {
        if (*client == '\0')
            continue;

        /* handle prefix stuff.  first find out where the prefixes end and the
         * nick begins,  prefixes can't be normal nick characters for obvious
         * reasons, so use this fact.. */
        prefix = client;
        while (*client && !istr_okay(ircd.maps.nick, client))
            client++;
        /* now we know what our prefixes are.. */

        cp = find_client(client);
        if (cp != NULL) {
            add_to_channel(cp, chan, true);
            if (changeok) /* if we made changes earlier, give them those
                             changes (which means the prefixed nick */
                strcat(realjoiners, prefix);
            else /* otherwise, just the nick! */
                strcat(realjoiners, client);
            strcat(realjoiners, " ");

            /* let our local users (and non-SJOIN servers) know they joined the
             * channel. */
            sendto_channel_local(chan, cp, NULL, "JOIN", NULL);
            sendto_serv_pflag_butone(PROTOCOL_SFL_SJOIN, false, srv, cp,
                    NULL, chan->name, "JOIN", NULL);
            if (changeok) {
                while (prefix != client) {
                    chanmode_setprefix(*prefix, chan, client, &i);
                    /* XXX: hardcoded prefixes :( */
                    if (*prefix == '@')
                        modes[mset++] = 'o';
                    else if (*prefix == '+')
                        modes[mset++] = 'v';
                    else
                        continue;
                    modes[mset] = '\0';
                    mlen += sprintf(&modebuf[mlen], " %s", client);
                    prefix++;
                }
                if (mset >= 6) { /* if we've got six (or more) modes to set */
                    /* okay... assume that non SJOIN servers also don't use
                     * TSMODE.. */
                    sendto_serv_pflag_butone(PROTOCOL_SFL_SJOIN, false, srv,
                           NULL, srv, chan->name, "MODE", "+%s%s %d", modes,
                           modebuf, chan->created); 
                    sendto_channel_local(chan, NULL, srv, "MODE", "+%s%s",
                            modes, modebuf);
                    mset = 0;
                    mlen = 0;
                }
            }
        } else
            log_warn("got SJOIN for unknown nick %s on %s", client,
                    chan->name);
    }

    /* if we have leftover modes, send them first */
    if (changeok && mset) {
        sendto_serv_pflag_butone(PROTOCOL_SFL_SJOIN, false, srv,
               NULL, srv, chan->name, "MODE", "+%s%s %d", modes,
               modebuf, chan->created); 
        sendto_channel_local(chan, NULL, srv, "MODE", "+%s%s",
                modes, modebuf);
    }

    /* now send along downstream.  we save the server the trouble of mangling
     * the 'joiners', but we must send them the modes and stuff, so build
     * modebuf quickly to contain extra args */
    i = 4;
    mlen = 0;
    while (i < (argc - 1))
        mlen += sprintf(&modebuf[mlen], " %s", argv[i++]);

    sendto_serv_pflag_butone(PROTOCOL_SFL_SJOIN, true, srv, NULL, srv, NULL,
            "SJOIN", "%d %s %s%s :%s", chan->created, chan->name, argv[3],
            modebuf, realjoiners);

    return 1;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
