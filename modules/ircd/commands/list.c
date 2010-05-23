/*
 * list.c: the LIST command
 * 
 * Copyright 2003 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 *
 * The LIST command has, of course, the great potential to knock you flat on
 * your ass off the network.  I've heard a few ideas for giving it a reasonable
 * query syntax that I wish to explore, but not right now.  For now I'm simply
 * including it as-is.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"
#include "commands/topic.h"

IDSTRING(rcsid, "$Id: list.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/core
*/

MODULE_LOADER(list) {

    /* numerics .. */
#define RPL_LISTSTART 321
    CMSG("321", "Channel Users :Topic");
#define RPL_LIST 322
    CMSG("322", "%s%s %d :%s%s");
#define RPL_LISTEND 323
    CMSG("323", ":End of /LIST");

    return 1;
}
MODULE_UNLOADER(list) {

    DMSG(RPL_LISTSTART);
    DMSG(RPL_LIST);
    DMSG(RPL_LISTEND);
}

/* the LIST command.  Currently very simple.  argv[1] might be a mask from the
 * user, in which case we match channels against that.  Otherwise we just send
 * them the whole list of channels. */
CLIENT_COMMAND(list, 0, 1, COMMAND_FL_REGISTERED) {
    char *mask = (argc > 1 ? argv[1] : NULL);
    const char **mgunk;
#define BUF_SIZE 320
    char buf[BUF_SIZE];
    channel_t *chan;
    struct channel_topic *ctp;
    int sent = 2;
    int see;

    sendto_one(cli, RPL_FMT(cli, RPL_LISTSTART));
    LIST_FOREACH(chan, ircd.lists.channels, lp) {
        if (mask == NULL || match(mask, chan->name)) {
            see = can_can_see_channel(cli, chan);
            if (see < 0) {
                ctp = TOPIC(chan);
                /* this is pretty stupid */
                if (BPRIV(cli, core.privs.see_hidden_chan)) {
                    mgunk = chanmode_getmodes(chan);
                    if (*mgunk[1] != '\0')
                        snprintf(buf, BUF_SIZE, "[%s %s] ", mgunk[0],
                                mgunk[1]);
                    else
                        snprintf(buf, BUF_SIZE, "[%s] ", mgunk[0]);
                } else
                    *buf = '\0';
#if 0
                sendto_one(cli, RPL_FMT(cli, RPL_LIST),
                        (see == CHANNEL_CHECK_OVERRIDE ? "%" : ""), chan->name,
                        chan->onchannel, buf, (ctp != NULL ? ctp->topic : ""));
#else
                sendto_one(cli, RPL_FMT(cli, RPL_LIST), "", chan->name,
                        chan->onchannel, buf, (ctp != NULL ? ctp->topic : ""));
#endif
                sent++;
            }
        }
    }
    sendto_one(cli, RPL_FMT(cli, RPL_LISTEND));

    return (sent < 100 ? COMMAND_WEIGHT_EXTREME : COMMAND_WEIGHT_MAX);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
