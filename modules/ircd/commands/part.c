/*
 * part.c: the PART command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: part.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

/*
 * argv[1] == channel(s) to part
 * argv[2] ?= part message
 */
CLIENT_COMMAND(part, 1, 2, 0) {
    char *msg = argc > 2 ? argv[2] : NULL;
    channel_t *chan;
    int parted = 0;
    char *buf, *name;
    int sendok;

    buf = argv[1];
    while ((name = strsep(&buf, ",")) != NULL) {
        if (*name == '\0')
            continue;

        parted++;
        if (!check_channame(name)) {
            sendto_one(cli, RPL_FMT(cli, ERR_BADCHANNAME), name);
            continue;
        }

        chan = find_channel(name);

        if (chan == NULL || !onchannel(cli, chan)) {
            sendto_one(cli, RPL_FMT(cli, ERR_NOTONCHANNEL), name);
            continue;
        }

        if (!CLIENT_MASTER(cli) && msg != NULL)
            sendok = can_can_send_channel(cli, chan, msg);
        else
            sendok = CHANNEL_CHECK_OK;

        /* send it to everyone locally in the channel, and also to all servers
         * except for sptr. */
        if (msg != NULL && sendok < 0) {
            sendto_channel_local(chan, cli, NULL, "PART", ":%s", msg);
            sendto_serv_butone(sptr, cli, NULL, chan->name, "PART", ":%s",
                    msg);
        } else {
            sendto_channel_local(chan, cli, NULL, "PART", NULL);
            sendto_serv_butone(sptr, cli, NULL, chan->name, "PART", NULL);
        }
        del_from_channel(cli, chan, true);
    }

    /* weight just like JOIN */
    return COMMAND_WEIGHT_HIGH + (parted * COMMAND_WEIGHT_LOW);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
