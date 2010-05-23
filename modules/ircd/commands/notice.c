/*
 * notice.c: the NOTICE (or PRIVMSG) command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "commands/away.h"

IDSTRING(rcsid, "$Id: notice.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/core
*/

#define MAXTARGETS 20 /* the traditional limit */
int maxtargets_priv;

static int do_send_notice(int, client_t *, server_t *, char *, char *);

MODULE_LOADER(notice) {
    int64_t i64 = MAXTARGETS;

    if (!get_module_savedata(savelist, "maxtargets_priv", &maxtargets_priv))
        maxtargets_priv = create_privilege("maxtargets", PRIVILEGE_FL_INT,
                &i64, NULL);
    add_isupport("MAXTARGETS", ISUPPORT_FL_PRIV, (char *)&maxtargets_priv);

    add_command_alias("notice", "privmsg");

#define ERR_NOTEXTTOSEND 412
    CMSG("412", ":No text to send.");

    return 1;
}
MODULE_UNLOADER(notice) {

    if (reload)
        add_module_savedata(savelist, "maxtargets_priv",
                sizeof(maxtargets_priv), &maxtargets_priv);
    else
        destroy_privilege(maxtargets_priv);

    del_isupport(find_isupport("MAXTARGETS"));

    DMSG(ERR_NOTEXTTOSEND);
}

/* argv[1] will be: recipient[,recipient,recipient,...], limit the number of
 * recipients via privileges system in class.
 * argv[2] will be the contents of the message. */

/* argv[0] will be whatever we were called as.  typically privmsg or notice,
 * though others could be added feasibly.. :) */
CLIENT_COMMAND(notice, 2, 2, COMMAND_FL_REGISTERED) {
    char *cur, *buf;
    int notice = (tolower(*argv[0]) == 'n');
    int targets = 0;
    int flood = 0;
    int maxtargets = (MYCLIENT(cli) ? IPRIV(cli, maxtargets_priv) : -1);

    if (*argv[2] == '\0') {
        sendto_one(cli, RPL_FMT(cli, ERR_NOTEXTTOSEND));
        return COMMAND_WEIGHT_LOW;
    }

    buf = reduce_string_list(argv[1], ",");
    while ((cur = strsep(&buf, ",")) != NULL) {
        targets++; /* increment targets no matter what */
        if (*cur == '\0')
            continue;

        /* only send if maxtargets isn't breached. */
        if (maxtargets < 1 || targets <= maxtargets)
            flood += do_send_notice(notice, cli, NULL, cur, argv[2]);
    }

    /* if they tried to send to too many targets let operators know */
    if (maxtargets > 0 && targets > maxtargets)
        sendto_flag(SFLAG("SPY"), "User %s (%s@%s) tried to message %d users "
                "at once (limited to %d)", cli->nick, cli->user, cli->host,
                targets, maxtargets);

    /* if they sent a privmsg command update their idle time */
    if (!notice)
        cli->last = me.now;
        
    /* Assign weight based on target counts */
    return flood;
}

/* the server notice (et al) command.  behaves a lot like the above, except
 * that servers cannot send comma separated lists of clients, and they are not
 * restricted in where their notices are sent.  also, while this technically
 * allows servers to send PRIVMSGs this behavior is unprecedented and should
 * probably never be done, and so I've assumed that no matter what we might
 * receivce the command as, the server means 'NOTICE'. */
SERVER_COMMAND(notice, 2, 2, 0) {

    /* we do not do comma-separation from servers.  maybe we should, but we do
     * not. */
    do_send_notice(1, NULL, srv, argv[1], argv[2]);
        
    return 0;
}

/* this handles the actual work of sending a notice (or privmsg) to a user,
 * channel, or god-knows-what-else. */
static int do_send_notice(int notice, client_t *cli, server_t *srv, char *to,
        char *msg) {
    client_t *target = NULL;
    channel_t *chan = NULL;
    server_t *server = NULL;
    int sendok = CLIENT_CHECK_OK;
    char *pcmd = (notice ? "NOTICE" : "PRIVMSG");
    char prefixes[64], *pfx = prefixes;
    char *serv;
    short pmask = 0;

    /* check for channel prefixes.  reduce the list to only one of each. */
    while (chanmode_prefixtomode(*to)) {
        struct chanmode *cmp = ircd.cmodes.pfxmap[(unsigned char)*to];
        if (cmp != NULL && !(pmask & cmp->umask)) {
            pmask |= cmp->umask;
            *pfx++ = *to;
        }
        to++;
    }
    *pfx = '\0';

    /* if it's a channel, try and find the channel, then send */
    if (check_channame(to)) {
        chan = find_channel(to);
        if (chan == NULL) {
            if (cli != NULL)
                sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHCHANNEL), to);
        } else {
            if (cli != NULL && !CLIENT_MASTER(cli)) {
                sendok = can_can_send_channel(cli, chan, msg);
                if (sendok > 0)
                    sendto_one(cli, RPL_FMT(cli, sendok), chan->name);
            } else
                sendok = CHANNEL_CHECK_OK;

            if (sendok < 0) {
                if (*prefixes)
                    sendto_channel_prefixes_butone(chan, cli, cli, srv,
                            prefixes, pcmd, ":%s", msg);
                else
                    sendto_channel_butone(chan, cli, cli, srv, pcmd, ":%s",
                            msg);
                return COMMAND_WEIGHT_MEDIUM;
            } /* silent failure otherwise. */
        }

        return 0; /* all done */
    } else if (*prefixes != '\0')
        return 0; /* otherwise, prefixed messages get swallowed. */

    /* not a channel, it could be a variety of other things:
     * client: a regular client send
     * client@server: a send to a client on a specific server
     * $$mask: send to all users on servers with the mask
     * $#mask: send to all users on hosts with the mask
     */
    if (*to == '$') {
        if (cli != NULL && MYCLIENT(cli) && !OPER(cli))
            sendto_one(cli, RPL_FMT(cli, ERR_NOPRIVILEGES));
        else
            sendto_match_butone(NULL, cli, srv, to + 1, pcmd, ":%s", msg);

        return 0;
    }

    /* see if they specified user@server */
    if ((serv = strchr(to, '@')) != NULL) {
        *serv++ = '\0';
        server = find_server(serv);
    }

    if ((target = find_client(to)) != NULL &&
            (server == NULL || target->server == server)) {
        /* only check if our client is sending or receiving, and the sender
         * isn't a master client. */
        if (cli != NULL && (MYCLIENT(target) || MYCLIENT(cli)) &&
                !CLIENT_MASTER(cli)) {
            sendok = can_can_send_client(cli, target, msg); 
            if (sendok > 1)
                sendto_one(cli, RPL_FMT(cli, sendok), target->nick);
        } else
            sendok = CLIENT_CHECK_OK;

        if (sendok < 0) {
            if (cli != NULL && MYCLIENT(cli) && AWAYMSG(target) != NULL &&
                    !notice)
                sendto_one(cli, RPL_FMT(cli, RPL_AWAY), target->nick,
                        AWAYMSG(target));
            sendto_one_from(target, cli, srv, pcmd, ":%s", msg);
            return COMMAND_WEIGHT_LOW;
        }

        return 0;
    }

    /* if we got this far, say that we had no luck processing their message. */
    if (cli != NULL)
        sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHNICK), to);

    return 0;
}
        
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
