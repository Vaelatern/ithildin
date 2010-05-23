/*
 * join.c: the JOIN command
 * 
 * Copyright 2002-2006 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: join.c 719 2006-04-25 12:10:16Z wd $");

MODULE_REGISTER("$Rev: 719 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/commands/part
*/

static int priv_maxchannels;
MODULE_LOADER(join) {
    int64_t i;

    /* privileges */
    i = 10;
    if (!get_module_savedata(savelist, "priv_maxchannels", &priv_maxchannels))
        priv_maxchannels = create_privilege("maxchannels", PRIVILEGE_FL_INT,
                &i, NULL);

    add_isupport("MAXCHANNELS", ISUPPORT_FL_PRIV, (char *)&priv_maxchannels);
#define ERR_TOOMANYCHANNELS 405
    CMSG("405", "%s :You have joined too many channels.");

    return 1;
}
MODULE_UNLOADER(join) {

    if (reload)
        add_module_savedata(savelist, "priv_maxchannels",
                sizeof(priv_maxchannels), &priv_maxchannels);
    else {
        destroy_privilege(priv_maxchannels);
        del_isupport(find_isupport("MAXCHANNELS"));
    }

    DMSG(ERR_TOOMANYCHANNELS);
}

/*
 * argv[1] == channel(s) to join (comma separated)
 * argv[2] ?= list of keys for channels
 */
CLIENT_COMMAND(join, 1, 2, 0) {
    channel_t *chan;
    struct chanlink *clp;
    int joined = 0;
    int new, okay;
    int i;
    int64_t i64;
    char *name, *buf;

    buf = argv[1];
    while ((name = strsep(&buf, ",")) != NULL) {
        if (*name == '\0')
            continue;

        new = okay = 0;
        joined++; /* increment even if it's just an attempt. */
        if (MYCLIENT(cli)) {
            if (!check_channame(name) && strcmp(name, "0")) {
                sendto_one(cli, RPL_FMT(cli, ERR_BADCHANNAME), name);
                continue;
            }

            /* see if they can join (check maxchannels) */
            i64 = IPRIV(cli, priv_maxchannels);
            i = 0;
            LIST_FOREACH(clp, &cli->chans, lpcli)
                i++; /* count channels.. */
            if (i64 > 0 && i >= i64) {
                /* too many channels.. */
                sendto_one(cli, RPL_FMT(cli, ERR_TOOMANYCHANNELS), name);
                continue;
            }
        } else if (SERVER_SUPPORTS(sptr, PROTOCOL_SFL_SJOIN))
            /* if it's not our client and it's from an SJOIN server... */
            log_warn("received JOIN for remote client! yuck!");

        if (!strcmp(name, "0")) {
            /* check for 'JOIN 0' legacy stuff.  this causes a part for
             * every channel the user is on.  ugh. ;) */

#if 1 /* CHANGE 1 to 0 IF YOU REALLY WANT LOCAL 'JOIN 0' SUPPORT */
            if (MYCLIENT(cli))
                continue;
#endif
            /* NOTE: Dreamforge (and maybe other garbage ircds) actually
             * send out, across the wire, to other servers, the 'JOIN 0'
             * bullshit so we must support it at all times.  We do the
             * right thing and send out PART commands down the wire as
             * need-be. */
            while ((clp = LIST_FIRST(&cli->chans)) != NULL) {
                char *fargv[2];

                fargv[0] = "PART";
                fargv[1] = clp->chan->name;
                switch ((i = command_exec_client(2, fargv, cli))) {
                case IRCD_PROTOCOL_CHANGED:
                case IRCD_CONNECTION_CLOSED:
                    return i;
                }
            }

            continue;
        }

        chan = find_channel(name);

        if (chan == NULL) {
            chan = create_channel(name);
            new = 1;
        } else if (onchannel(cli, chan))
            continue; /* silently ignore */

        add_to_channel(cli, chan, false);
        /* uh, actually.  always execute the checks to see if they can join...
         * well, for local clients anyhow.  never check non-local clients! */
        if (MYCLIENT(cli) && (okay = can_can_join_channel(cli, chan,
                        (argc > 2 ? argv[2] : NULL))) >= 0) {
            /* if it wasn't successful, send them an error message (if
             * specified), then delete them from the channel again. */
            if (okay != 0)
                sendto_one(cli, RPL_FMT(cli, okay), chan->name);
            del_from_channel(cli, chan, false);
            continue;
        }

        if (MYCLIENT(cli) && new) {
            /* set up the channel with some stuff */
            chanmode_setprefix('@', chan, cli->nick, &i); /* op them. */
        }

        /* always send a local message, and always send SJOINs when we can. */
        sendto_channel_local(chan, cli, NULL, "JOIN", NULL);
        if (new)
            sendto_serv_pflag_butone(PROTOCOL_SFL_SJOIN, true, sptr, NULL,
                    cli->server, NULL, "SJOIN", "%d %s + :%s%s", chan->created,
                    chan->name, chanmode_getprefixes(chan, cli), cli->nick);
        else
            sendto_serv_pflag_butone(PROTOCOL_SFL_SJOIN, true, sptr, cli,
                    NULL, NULL, "SJOIN", "%d %s", chan->created,
                    chan->name);
        sendto_serv_pflag_butone(PROTOCOL_SFL_SJOIN, false, sptr, cli,
                NULL, NULL, "JOIN", "%s", chan->name);

        if (MYCLIENT(cli) && new)
            /* don't forget to send that +o .. :) */
            sendto_serv_pflag_butone(PROTOCOL_SFL_SJOIN, false, NULL, NULL,
                    ircd.me, chan->name, "MODE", "+%c %s",
                    chanmode_prefixtomode('@'), cli->nick);

        /* we have to manually hook this because we ask for it not to be hooked
         * above. */
        hook_event(ircd.events.channel_add, LIST_FIRST(&cli->chans));
    }

    /* Provide a basic weight, and some small weight for each additional
     * channel joined past the first */
    return COMMAND_WEIGHT_MEDIUM + ((joined - 1) * COMMAND_WEIGHT_LOW);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
