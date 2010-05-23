/*
 * away.c: the AWAY command.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "commands/away.h"
#include "commands/whois.h"

IDSTRING(rcsid, "$Id: away.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/commands/whois ircd/addons/core
*/

HOOK_FUNCTION(away_whois_hook);
HOOK_FUNCTION(away_server_establish_hook);
HOOK_FUNCTION(away_mdext_hook);

int priv_awaylen;

MODULE_LOADER(away) {
    int64_t i = TOPICLEN;

    priv_awaylen = create_privilege("away-length", PRIVILEGE_FL_INT, &i, NULL);
    if (!get_module_savedata(savelist, "away_mdext", &away_mdext))
        away_mdext = create_mdext_item(ircd.mdext.client, sizeof(char *));

    add_hook(whois_event, away_whois_hook);
    add_hook(ircd.events.server_establish, away_server_establish_hook);
    add_hook(ircd.mdext.client->destroy, away_mdext_hook);

    CMSG("301", "%s :%s");
#define RPL_UNAWAY 305
    CMSG("305", ":You are no longer marked as being away.");
#define RPL_NOWAWAY 306
    CMSG("306", ":You have been marked as being away");

    return 1;
}
MODULE_UNLOADER(away) {

    destroy_privilege(priv_awaylen);
    if (reload)
        add_module_savedata(savelist, "away_mdext", sizeof(away_mdext),
                &away_mdext);
    else
        destroy_mdext_item(ircd.mdext.client, away_mdext);

    away_mdext = NULL;
    remove_hook(whois_event, away_whois_hook);
    remove_hook(ircd.events.server_establish, away_server_establish_hook);
    remove_hook(ircd.mdext.client->destroy, away_mdext_hook);

    DMSG(RPL_AWAY);
    DMSG(RPL_UNAWAY);
    DMSG(RPL_NOWAWAY);
}

/* argv[1] ?= message to set.  if there are no arguments, the away message is
 * removed. */
CLIENT_COMMAND(away, 0, 1, COMMAND_FL_REGISTERED) {
    int len;
    int maxlen;
    char **msg = (char **)mdext(cli, away_mdext);
    
    if (AWAYMSG(cli) != NULL) {
        free(*msg);
        *msg = NULL;
    }

    /* oops.  argc should be less than 2 (no arguments), but it's also
     * acceptable to send an empty first argument ('AWAY :'). */
    if (argc < 2 || *argv[1] == '\0') {
        /* they're just unsetting away, let them know and propogate. */
        if (MYCLIENT(cli))
            sendto_one(cli, RPL_FMT(cli, RPL_UNAWAY));
        sendto_serv_butone(sptr, cli, NULL, NULL, "AWAY", NULL);
    } else {
        len = strlen(argv[1]) + 1;
        if (MYCLIENT(cli)) {
            maxlen = IPRIV(cli, priv_awaylen);
            if (maxlen > 0 && len > maxlen)
                len = maxlen;
        }
        *msg = malloc(len);
        strlcpy(*msg, argv[1], len);

        if (MYCLIENT(cli))
            sendto_one(cli, RPL_FMT(cli, RPL_NOWAWAY));
        sendto_serv_butone(sptr, cli, NULL, NULL, "AWAY", ":%s", AWAYMSG(cli));
    }

    return COMMAND_WEIGHT_HIGH;
}

/* ulch.  some servers send the AWAY command with the first argument as the
 * user and the second argument as the message.  blech. */
SERVER_COMMAND(away, 2, 2, 0) {
    client_t *cp;
    int len;
    char **msg;
    
    if ((cp = find_client(argv[1])) == NULL)
        return 0;

    msg = (char **)mdext(cp, away_mdext);
    if (AWAYMSG(cp) != NULL) {
        free(*msg);
        *msg = NULL;
    }
    len = strlen(argv[2]) + 1;
    *msg = malloc(len);
    strlcpy(*msg, argv[2], len);

    sendto_serv_butone(sptr, cp, NULL, NULL, "AWAY", ":%s", AWAYMSG(cp));

    return 0;
}

HOOK_FUNCTION(away_whois_hook) {
    client_t *cli = cptr.cli;
    client_t *target = (client_t *)data;

    if (AWAYMSG(target) != NULL)
        sendto_one(cli, RPL_FMT(cli, RPL_AWAY), target->nick, AWAYMSG(target));

    return NULL;
}

/* this is used to synchronize away messages across a network. */
HOOK_FUNCTION(away_server_establish_hook) {
    server_t *to = (server_t *)data;
    client_t *cp;

    LIST_FOREACH(cp, ircd.lists.clients, lp) {
        if (cli_server_uplink(cp) != to && AWAYMSG(cp))
            sendto_serv_from(to, cp, NULL, NULL, "AWAY", ":%s", AWAYMSG(cp));
    }

    return NULL;
}

HOOK_FUNCTION(away_mdext_hook) {
    char **msg = (char **)mdext_offset(data, away_mdext);

    if (*msg != NULL) {
        free(*msg);
        *msg = NULL;
    }

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
