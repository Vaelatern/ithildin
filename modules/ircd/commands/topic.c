/*
 * topic.c: the TOPIC command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"
#include "commands/topic.h"

IDSTRING(rcsid, "$Id: topic.c 752 2006-06-24 18:22:28Z wd $");

MODULE_REGISTER("$Rev: 752 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/core
*/

unsigned char topic_chanmode;
HOOK_FUNCTION(topic_server_establish_hook);
HOOK_FUNCTION(topic_channel_add_hook);

MODULE_LOADER(topic) {
    int64_t i64;

    if (!get_module_savedata(savelist, "topic_chanmode", &topic_chanmode))
        chanmode_request('t', &topic_chanmode, CHANMODE_FL_D,
                "chanmode_flag", "chanmode_flag_query", 0, NULL);
    if (!get_module_savedata(savelist, "topic_mdext", &topic_mdext))
        topic_mdext = create_mdext_item(ircd.mdext.channel,
                sizeof(struct channel_topic));

    i64 = TOPICLEN;
    add_isupport("TOPICLEN", ISUPPORT_FL_INT, (char *)&i64);

    /* stuff this hook in nice and early so we are always in front of NAMES.
     * THis should be harmless, at least for now.  Some clients seem to
     * depend on speculative wording in RFC1459 that says RPL_TOPIC and
     * RPL_NAMREPLY are sent.  They believe this implies that RPL_TOPIC must
     * be first.  Most clients do not break when this isn't the case, but a
     * few poorly written ones do. */
    add_hook_before(ircd.events.channel_add, topic_channel_add_hook, NULL);

    add_hook(ircd.events.server_establish, topic_server_establish_hook);

#define RPL_NOTOPIC 331
    CMSG("331", "%s :No topic is set.");
#define RPL_TOPIC 332
    CMSG("332", "%s :%s");
#define RPL_TOPICWHOTIME 333
    CMSG("333", "%s %s %lu");

    return 1;
}
MODULE_UNLOADER(topic) {

    if (reload) {
        add_module_savedata(savelist, "topic_chanmode", sizeof(topic_chanmode),
                &topic_chanmode);
        add_module_savedata(savelist, "topic_mdext", sizeof(topic_mdext),
                &topic_mdext);
    } else {
        chanmode_release(topic_chanmode);
        destroy_mdext_item(ircd.mdext.channel, topic_mdext);
    }

    del_isupport(find_isupport("TOPICLEN"));

    remove_hook(ircd.events.channel_add, topic_channel_add_hook);
    remove_hook(ircd.events.server_establish, topic_server_establish_hook);

    DMSG(RPL_NOTOPIC);
    DMSG(RPL_TOPIC);
    DMSG(RPL_TOPICWHOTIME);
}

/* the topic command.  may come from either clients or servers.
 * argv[1] == channel (local users)
 * argv[2] == topic (local users) or person who set the topic (remote)
 * argv[3] == topic timestamp (remote)
 * argv[4] == topic (remote)
 */
CLIENT_COMMAND(topic, 1, 4, 0) {
    channel_t *chan = find_channel(argv[1]);
    struct channel_topic *ctp;

    if (chan == NULL) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHCHANNEL), argv[1]);
        return COMMAND_WEIGHT_NONE;
    }
    
    ctp = TOPIC(chan);
    if (MYCLIENT(cli)) {
        if (argc < 3) {
            int see;
            char modname[CHANLEN + 2];

            see = can_can_see_channel(cli, chan);
            if (see == CHANNEL_CHECK_OVERRIDE)
                sprintf(modname, "%%%s", chan->name);
            else if (see < 0)
                strcpy(modname, chan->name);
            else if (see > 0) {
                sendto_one(cli, RPL_FMT(cli, see), chan->name);
                return COMMAND_WEIGHT_NONE;
            } else
                return COMMAND_WEIGHT_NONE; /* silently denied. */
            
            if (*ctp->topic) {
                sendto_one(cli, RPL_FMT(cli, RPL_TOPIC), modname,
                        ctp->topic);
                sendto_one(cli, RPL_FMT(cli, RPL_TOPICWHOTIME), modname,
                        ctp->by, ctp->set);
            } else 
                sendto_one(cli, RPL_FMT(cli, RPL_NOTOPIC), modname);
            return COMMAND_WEIGHT_LOW;
        }
        if (!onchannel(cli, chan)) {
            sendto_one(cli, RPL_FMT(cli, ERR_NOTONCHANNEL), chan->name);
            return COMMAND_WEIGHT_NONE;
        }
        if (chanmode_isset(chan, topic_chanmode) && !CHANOP(cli, chan) &&
                !CLIENT_MASTER(cli)) {
            sendto_one(cli, RPL_FMT(cli, ERR_CHANOPRIVSNEEDED), chan->name);
            return COMMAND_WEIGHT_NONE;
        }
        strncpy(ctp->topic, argv[2], TOPICLEN);
        strncpy(ctp->by, cli->nick, ircd.limits.nicklen);
        ctp->set = me.now;
    } else {
        /* a remote client. */
        if (argc != 5)
            return 0; /* uhh.. */
        strncpy(ctp->by, argv[2], ircd.limits.nicklen);
        ctp->set = str_conv_int(argv[3], 0);
        strncpy(ctp->topic, argv[4], TOPICLEN);
    }
    
    sendto_channel_local(chan, cli, NULL, "TOPIC", ":%s", ctp->topic);
    sendto_serv_butone(sptr, cli, NULL, chan->name, "TOPIC",
            "%s %d :%s", ctp->by, ctp->set, ctp->topic);

    return COMMAND_WEIGHT_MEDIUM;
}

/* just like the client command, but always assumes remote. */
SERVER_COMMAND(topic, 4, 4, 0) {
    channel_t *chan = find_channel(argv[1]);
    struct channel_topic *ctp;
    time_t ts;

    if (chan == NULL)
        return 0; /* hm.. */
    ctp = TOPIC(chan);
    ts = str_conv_int(argv[3], 0);

    /* (this is what bahamut does, I'm not convinced it is correct): if the
     * local topic is newer than the remote topic and we have a topic and we're
     * in a sync (server setting the topic) then don't allow the change. */
    if (ctp->set >= ts && *ctp->topic)
        return 0; /* huh.. */

    strncpy(ctp->by, argv[2], ircd.limits.nicklen);
    ctp->set = ts;
    strncpy(ctp->topic, argv[4], TOPICLEN);
    
    sendto_channel_local(chan, NULL, srv, "TOPIC", ":%s", ctp->topic);
    sendto_serv_butone(srv, NULL, srv, chan->name, "TOPIC",
            "%s %d :%s", ctp->by, ctp->set, ctp->topic);

    return 0;
}

/* burst topics to the connecting server .. */
HOOK_FUNCTION(topic_server_establish_hook) {
    server_t *to = (server_t *)data;
    channel_t *cp;
    struct channel_topic *ctp;

    LIST_FOREACH(cp, ircd.lists.channels, lp) {
        ctp = TOPIC(cp);
        if (*ctp->topic)
            sendto_serv_from(to, NULL, ircd.me, cp->name, "TOPIC",
                    "%s %d :%s", ctp->by, ctp->set, ctp->topic);
    }

    return NULL;
}

/* display the topic to local clients joining a channel */
HOOK_FUNCTION(topic_channel_add_hook) {
    struct chanlink *clp = (struct chanlink *)data;
    struct channel_topic *ctp = TOPIC(clp->chan);

    if (!MYCLIENT(clp->cli) || *ctp->topic == '\0')
        return NULL;

    sendto_one(clp->cli, RPL_FMT(clp->cli, RPL_TOPIC), clp->chan->name,
            ctp->topic);
    sendto_one(clp->cli, RPL_FMT(clp->cli, RPL_TOPICWHOTIME), clp->chan->name,
            ctp->by, ctp->set);
        
    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
