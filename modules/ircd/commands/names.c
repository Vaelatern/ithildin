/*
 * names.c: the NAMES command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: names.c 802 2007-03-22 04:13:51Z wd $");

MODULE_REGISTER("$Rev: 802 $");
/*
@DEPENDENCIES@: ircd
*/

HOOK_FUNCTION(names_join_hook);
void do_names(client_t *, channel_t *);

MODULE_LOADER(names) {

    add_hook(ircd.events.channel_add, names_join_hook);

    /* now create numerics */
#define RPL_NAMREPLY 353
    CMSG("353", "%c %s :%s");
#define RPL_ENDOFNAMES 366
    CMSG("366", "%s :End of /NAMES list.");

    return 1;
}
MODULE_UNLOADER(names) {

    remove_hook(ircd.events.channel_add, names_join_hook);

    DMSG(RPL_NAMREPLY);
    DMSG(RPL_ENDOFNAMES);
}

/* argv[1] = channel to get /names for.  /names 1,2,3 is not supported,
 * neither is masking channels. */
CLIENT_COMMAND(names, 1, 1, COMMAND_FL_REGISTERED) {
    channel_t *chan;
    int see;

    if (!check_channame(argv[1])) {
        sendto_one(cli, RPL_FMT(cli, ERR_BADCHANNAME), argv[1]);
        return COMMAND_WEIGHT_NONE;
    }
        
    chan = find_channel(argv[1]);
    if (chan == NULL) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHCHANNEL), argv[1]);
        return COMMAND_WEIGHT_NONE;
    }

    if ((see = can_can_see_channel(cli, chan)) > 0) {
        sendto_one(cli, RPL_FMT(cli, see), chan->name);
        return COMMAND_WEIGHT_NONE;
    }

    do_names(cli, chan);
    return COMMAND_WEIGHT_HIGH;
}

/* actually do the /names reply thing */
void do_names(client_t *cli, channel_t *chan) {
    struct chanlink *clp;
#define NAMEBUFLEN 300
    char buf[NAMEBUFLEN + 20];
    int len = 0;
    int see = onchannel(cli, chan);

    *buf = '\0';

    /* now, for all the channel members, walk the list and send an
     * RPL_NAMREPLY when our buffer gets full.  /NAMES is really a bad
     * command for large channels :/ */
    LIST_FOREACH(clp, &chan->users, lpchan) {
        if (INVIS(clp->cli) && !see)
            continue;
        if (NAMEBUFLEN < len + ircd.limits.nicklen) {
            /* avoid potential overflow, dump our buffer soon.  for networks
             * with really long name support (like DALnet's 30 chars), you can
             * see incredibly shitty results if all the members of a channel
             * have long nicks, namely that if everyone maxes out nick length,
             * you only send 10 nicks per /names reply!  In a big channel,
             * that's basically death :/ */
            buf[len - 1] = '\0';
            sendto_one(cli, RPL_FMT(cli, RPL_NAMREPLY), '=', chan->name, buf);
            len = 0;
        }
        len += sprintf(&buf[len], "%s%s ",
                chanmode_getprefixes(chan, clp->cli), clp->cli->nick);
    }
    if (len) {
        buf[len - 1] = '\0';
        sendto_one(cli, RPL_FMT(cli, RPL_NAMREPLY), '=', chan->name, buf);
    }

    sendto_one(cli, RPL_FMT(cli, RPL_ENDOFNAMES), chan->name);
}

/* send /NAMES to rfc1459 users when they join a channel. */
HOOK_FUNCTION(names_join_hook) {
    struct chanlink *clp = (struct chanlink *)data;

    if (clp->cli->conn != NULL &&
            !strcmp(clp->cli->conn->proto->name, "rfc1459"))
        do_names(clp->cli, clp->chan);

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
