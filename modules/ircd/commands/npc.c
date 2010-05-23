/*
 * npc.c: the NPC command
 * 
 * Copyright 2009 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"

IDSTRING(rcsid, "$Id: topic.c 752 2006-06-24 18:22:28Z wd $");

MODULE_REGISTER("$Rev: 752 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/core
*/

#define NPC_ACTION_COMMAND "NPCA"

unsigned char npc_chanmode;

MODULE_LOADER(npc) {
    if (!get_module_savedata(savelist, "npc_chanmode", &npc_chanmode))
        chanmode_request('P', &npc_chanmode, CHANMODE_FL_D,
                "chanmode_flag", "chanmode_flag_query", 0, NULL);

    add_command_alias("npc", NPC_ACTION_COMMAND);

    return 1;
}

MODULE_UNLOADER(npc) {

    if (reload)
        add_module_savedata(savelist, "npc_chanmode", sizeof(npc_chanmode),
                &npc_chanmode);
    else
        chanmode_release(npc_chanmode);
}

/* the npc command.  may come from clients only
 * argv[1] == channel
 * argv[2] == npc name
 * argv[3] == npc text
 */
CLIENT_COMMAND(npc, 3, 3, COMMAND_FL_REGISTERED | COMMAND_FL_FOLDMAX) {
    static client_t fake_client;

    bool action = !strcasecmp(argv[0], NPC_ACTION_COMMAND);
    channel_t *chan = find_channel(argv[1]);
    int sendok = CLIENT_CHECK_OK;
    size_t message_space = 512;
    size_t message_length = 0;

    /* setup our fake client if necessary */
    if (fake_client.signon == 0) {
        fake_client.signon = me.now;
        fake_client.server = ircd.me;
        fake_client.flags |= IRCD_CLIENT_REGISTERED;
        strcpy(fake_client.user, "npc");
    }
    strlcpy(fake_client.host, cli->server->name, HOSTLEN);

    if (chan == NULL) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHCHANNEL), argv[1]);
        return COMMAND_WEIGHT_LOW;
    } else if (!chanmode_isset(chan, npc_chanmode)) {
        sendto_one(cli, RPL_FMT(cli, ERR_CANNOTSENDTOCHAN), argv[1]);
        return COMMAND_WEIGHT_LOW;
    } else if (!CLIENT_MASTER(cli)) {
        sendok = can_can_send_channel(cli, chan, argv[3]);
        if (sendok > 0) {
            sendto_one(cli, RPL_FMT(cli, sendok), chan->name);
            return COMMAND_WEIGHT_MEDIUM;
        }
    }

    /* copy as much of the nick as possible, reserve space for underscoring */
    if (find_client(argv[2]) != NULL) {
        sendto_one(cli, "NOTICE", ":NPC name %s is in use by an existing user and "
                   "is not available at this time.", argv[2]);
        return COMMAND_WEIGHT_LOW;
    } else if (strlen(argv[2]) > NICKLEN - 2) {
        sendto_one(cli, "NOTICE", ":NPC name %s is too long (maximum is %d letters)",
                   argv[2], NICKLEN - 2);
        return COMMAND_WEIGHT_LOW;
    } else {
        if (can_can_nick_client(cli, argv[2]) >= 0 || !check_nickname(argv[2])) {
            sendto_one(cli, "NOTICE", ":NPC name %s is not available for use.",
                       argv[2]);
            return COMMAND_WEIGHT_LOW;
        }
    }

    snprintf(fake_client.nick, NICKLEN, "%s", argv[2]);

    if (MYCLIENT(cli)) {
        /* calculate against: ":nick!user@host PRIVMSG #channel :blah (Sender)\r\n"
         * which is the most antiquated way of passing messages. */
        message_space -= 19; /* ":!@ PRIVMSG  : ()\r\n" = 19 */
        message_space -= (strlen(fake_client.nick) + strlen(fake_client.user) +
                          strlen(fake_client.host));
        message_space -= strlen(chan->name);
        message_space -= strlen(cli->nick);
        if (action)
            message_space -= 9; /* "\001ACTION \001" */
        
        message_length = strlen(argv[3]);
        if (MYCLIENT(cli) && message_length > message_space) {
            sendto_one(cli, "NOTICE", ":Your NPC message (beginning \"%.30s ...\") "
                       "was too long.  Please remove %u characters to send it.",
                       argv[3], message_length - message_space);
            return COMMAND_WEIGHT_MEDIUM;
        }
    }
    
    sendto_channel_local(chan, &fake_client, NULL, "PRIVMSG", ":%s%s (%s)%s",
                         (action ? "\001ACTION " : ""),
                         argv[3], cli->nick, (action ? "\001" : ""));
    /* XXX: need to switch to sendto_channel_remote at some point! */
    sendto_serv_butone(sptr, cli, NULL, chan->name, argv[0], "%s :%s",
                       argv[2], argv[3]);

    return COMMAND_WEIGHT_MEDIUM;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
