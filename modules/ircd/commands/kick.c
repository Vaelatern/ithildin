/*
 * kick.c: the KICK command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"

IDSTRING(rcsid, "$Id: kick.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/core
*/

MODULE_LOADER(kick) {
    int64_t i64 = TOPICLEN;

    add_isupport("KICKLEN", PRIVILEGE_FL_INT, (char *)&i64);

    return 1;
}
MODULE_UNLOADER(kick) {

    del_isupport(find_isupport("KICKLEN"));
}

#define KICK_MAX 4
/* the kick command.  clients only.
 * argv[1] == channel
 * argv[2] == client(s) to kick (may be comma separated)
 * argv[3] ?= comment */
CLIENT_COMMAND(kick, 2, 3, 0) {
    char *msg = argc > 3 ? argv[3] : cli->nick; /* the default msg is their
                                                   nickname. */
    channel_t *chan;
    char *next, *cur;
    int count = 1;
    client_t *cp;

    if (!check_channame(argv[1])) {
        sendto_one(cli, RPL_FMT(cli, ERR_BADCHANNAME), argv[1]);
        return COMMAND_WEIGHT_LOW;
    }

    chan = find_channel(argv[1]);

    if (!CLIENT_MASTER(cli) && !onchannel(cli, chan)) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOTONCHANNEL), argv[1]);
        return COMMAND_WEIGHT_LOW;
    }
    /* if they're my client and not opped, say no */
    if (MYCLIENT(cli) && !CHANOP(cli, chan)) {
        sendto_one(cli, RPL_FMT(cli, ERR_CHANOPRIVSNEEDED), chan->name);
        return COMMAND_WEIGHT_LOW;
    }

    cur = argv[2];
    next = strchr(cur, ',');
    while (cur != NULL) {
        if (next != NULL)
            *next++ = '\0';

        /* chase for /kicks */
        cp = client_get_history(cur, 0);
        if (cp != NULL) {
            if (onchannel(cp, chan)) {
                /* be sure to propogate all over. */
                sendto_channel_local(chan, cli, NULL, "KICK", "%s :%s",
                        cp->nick, msg);
                sendto_serv_butone(sptr, cli, NULL, chan->name, "KICK",
                        "%s :%s", cp->nick, msg);
                del_from_channel(cp, chan, true);
            } else
                sendto_one(cli, RPL_FMT(cli, ERR_USERNOTINCHANNEL), cp->nick,
                        chan->name);
        } else
            sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHNICK), cur);

        if (++count > KICK_MAX) /* stop after KICK_MAX people */
            break;
        else if (next != NULL && *next) {
            cur = next;
            next = strchr(cur, ',');
            continue;
        }
        break;
    }

    return COMMAND_WEIGHT_HIGH;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
