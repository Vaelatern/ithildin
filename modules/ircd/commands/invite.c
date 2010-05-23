/*
 * invite.c: the INVITE command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"

IDSTRING(rcsid, "$Id: invite.c 613 2005-11-22 13:43:19Z wd $");

MODULE_REGISTER("$Rev: 613 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/core
*/

/* this structure is used to hold invites to a channel. */
SLIST_HEAD(channel_invite_list, channel_invite);
struct channel_invite {
    client_t  *cli;   /* the client invited */
    time_t at;            /* when they were invited */

    SLIST_ENTRY(channel_invite) lp;
};
/* this is the maximum number of invites per channel.  it might be better as
 * a conf setting, if anyone is interested. */
#define MAX_INVITES_PER_CHANNEL 5

CHANMODE_FUNC(chanmode_i);
HOOK_FUNCTION(can_join_mode_i);
static unsigned char chanmode_invite;

MODULE_LOADER(invite) {

    if (!get_module_savedata(savelist, "chanmode_invite", &chanmode_invite)) {
        EXPORT_SYM(chanmode_i);
        chanmode_request('i', &chanmode_invite, CHANMODE_FL_D, "chanmode_i",
                "chanmode_flag_query", sizeof(struct channel_invite_list), NULL);
    }

    add_hook(ircd.events.can_join_channel, can_join_mode_i);

#define RPL_INVITING 341
    CMSG("341", "%s %s");
#define ERR_USERONCHANNEL 443
    CMSG("443", "%s %s :is already on channel");
#define ERR_INVITEONLYCHAN 473
    CMSG("473", "%s :Cannot join channel (+i)");

    return 1;
}
MODULE_UNLOADER(invite) {

    if (reload)
        add_module_savedata(savelist, "chanmode_invite",
                sizeof(chanmode_invite), &chanmode_invite);
    else
        chanmode_release(chanmode_invite);

    remove_hook(ircd.events.can_join_channel, can_join_mode_i);

    DMSG(RPL_INVITING);
    DMSG(ERR_USERONCHANNEL);
    DMSG(ERR_INVITEONLYCHAN);
}

/*
 * argv[1] == channel to invite into
 * argv[2] == user to invite
 */
CLIENT_COMMAND(invite, 2, 2, COMMAND_FL_REGISTERED) {
    client_t *cp;
    channel_t *chp;
    struct chanlink *clp;

    if ((cp = find_client(argv[1])) == NULL) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHNICK), argv[1]);
        return COMMAND_WEIGHT_LOW;
    }

    /* old daemons allowed people to invite to nonexistant channels.  squash
     * that.  it seems bogus to me. */
    if ((chp = find_channel(argv[2])) == NULL) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHCHANNEL), argv[2]);
        return COMMAND_WEIGHT_LOW;
    }

    if (!CLIENT_MASTER(cli) && (clp = find_chan_link(cli, chp)) == NULL) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOTONCHANNEL), argv[2]);
        return COMMAND_WEIGHT_LOW;
    }

    if (onchannel(cp, chp)) {
        sendto_one(cli, RPL_FMT(cli, ERR_USERONCHANNEL), cp->nick, chp->name);
        return COMMAND_WEIGHT_LOW;
    }

    if (!CLIENT_MASTER(cli) && !CHANOP(cli, chp)) {
        sendto_one(cli, RPL_FMT(cli, ERR_CHANOPRIVSNEEDED), chp->name);
        return COMMAND_WEIGHT_LOW;
    }

    if (MYCLIENT(cli)) {
        sendto_one(cli, RPL_FMT(cli, RPL_INVITING), cp->nick, chp->name);
    }

    sendto_one_from(cp, cli, NULL, "INVITE", ":%s", chp->name);

    if (MYCLIENT(cp)) {
        int i = 0; /* count of how many invites we have. */
        struct channel_invite_list *ilist =
            (struct channel_invite_list *)chanmode_getdata(chp, 'i');
        struct channel_invite *ip = NULL;

        sendto_channel_prefixes_butone(chp, NULL, NULL, ircd.me, "@",
                "NOTICE", ":%s invited %s into channel %s.", cli->nick,
                cp->nick, chp->name);

        SLIST_FOREACH(ip, ilist, lp) {
            i++;
            if (ip->cli == cp) {
                ip->at = me.now;
                return COMMAND_WEIGHT_HIGH; /* just update it. */
            } else if (SLIST_NEXT(ip, lp) == NULL)
                break; /* make ip the last item on the chain. */
        }
        if (i >= MAX_INVITES_PER_CHANNEL)
            SLIST_REMOVE(ilist, ip, channel_invite, lp);
        ip = malloc(sizeof(struct channel_invite));
        ip->cli = cp;
        ip->at = me.now;
        SLIST_INSERT_HEAD(ilist, ip, lp);
    }

    return COMMAND_WEIGHT_HIGH;
}

CHANMODE_FUNC(chanmode_i) {
    struct channel_invite_list *cilp =
        (struct channel_invite_list *)chanmode_getdata(chan, mode);
    struct channel_invite *cip;

    if (set == CHANMODE_CLEAR) {
            while (!SLIST_EMPTY(cilp)) {
                cip = SLIST_FIRST(cilp);
                SLIST_REMOVE_HEAD(cilp, lp);
                free(cip);
            }
    } else
        return chanmode_flag(cli, chan, mode, set, arg, argused);

    return CHANMODE_OK;
}

HOOK_FUNCTION(can_join_mode_i) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;
    struct channel_invite_list *ilist =
        (struct channel_invite_list *)chanmode_getdata(ccap->chan, 'i');
    struct channel_invite *ip;
    void *ret = (void *)HOOK_COND_OK; /* accept by default. */

    if (chanmode_isset(ccap->chan, 'i'))
        ret = (void *)ERR_INVITEONLYCHAN;

    /* we always walk the invite list, because invites can be used to bypass
     * other stuff even if the channel isn't +i.  Kind of shifty, I guess, but
     * hey. */
    SLIST_FOREACH(ip, ilist, lp) {
        if (ip->cli == ccap->cli) {
            /* only allow this through if they are the same and the signon
             * time is appropriate */
            if (ccap->cli->signon <= ip->at) {
                ret = (void *)HOOK_COND_ALWAYSOK; /* you can invite through
                                                     just about anything. */
                ccap->clp->bans = 0; /* nullify bans. */
            }
            /* remove the entry and free it, no matter what */
            SLIST_REMOVE(ilist, ip, channel_invite, lp);
            free(ip);
            break;
        }
    }

    return ret;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
