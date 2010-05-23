/*
 * kill.c: the KILL command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: kill.c 718 2006-04-25 11:44:28Z wd $");

MODULE_REGISTER("$Rev: 718 $");
/*
@DEPENDENCIES@: ircd
*/

static struct privilege_tuple priv_tuple[] = {
#define KILL_LOCAL 0
    { "local",        KILL_LOCAL },
#define KILL_GLOBAL 1
    { "global",        KILL_GLOBAL },
    { "remote",        KILL_GLOBAL }, /* a synonym for 'global' */
    { NULL,        0 }
};

static int kill_priv;
static int kill_sflag;
static int servkill_sflag;

MODULE_LOADER(kill) {
    int64_t i64 = 0;

    kill_priv = create_privilege("kill", PRIVILEGE_FL_TUPLE, &i64,
            &priv_tuple);
    kill_sflag = create_send_flag("KILLS", 0, -1);
    servkill_sflag = create_send_flag("SERVERKILLS", 0, -1);

    return 1;
}
MODULE_UNLOADER(kill) {

    destroy_privilege(kill_priv);
    destroy_send_flag(kill_sflag);
    destroy_send_flag(servkill_sflag);
}

/*
 * kill command.  local opers can kill a comma separated list of users.
 * argv[1] == user(s) to kill
 * argv[2] == kill message
 */
CLIENT_COMMAND(kill, 2, 2, COMMAND_FL_OPERATOR) {
    client_t *cp;
    char msg[TOPICLEN + 1];
    char path[TOPICLEN + 1];
    char *cur, *buf;
    char *s;
    bool local;
    int chasing;
    int count = 0;

#define KILL_MAX 20
    /* argv[1] may be comma-separated, go through it and find each client. */
    buf = argv[1];
    while ((cur = strsep(&buf, ",")) != NULL) {
        if (*cur == '\0')
            continue;
        chasing = 0;
        local = false;
        cp = find_client(cur);
        if (cp == NULL) {
            cp = client_get_history(cur, 0);
            if (cp == NULL) {
                sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHNICK), cur);
                continue;
            } 
            chasing = 1;
            sendto_one(cli, "NOTICE", ":KILL changed from %s to %s", cur,
                    cp->nick);
        }

        if (MYCLIENT(cli)) {
            local = MYCLIENT(cp);
            if (TPRIV(cli, kill_priv) == KILL_LOCAL && !local) {
                sendto_one(cli, RPL_FMT(cli, ERR_NOPRIVILEGES));
                continue;
            }
        }
        /* okay, they can do it. */
        
        /* construct the path message if need be */
        if (MYCLIENT(cli)) {
            /* if the message is coming from us, we set up the path */
            snprintf(path, TOPICLEN, "%s!%s!%s (%s)", ircd.me->name, cli->host,
                    cli->nick, argv[2]);
        } else /* copy in our path .. */
            strlcpy(path, argv[2], TOPICLEN + 1);

        /* send a notice about the kill (send the real host to operators) */
        sendto_flag_priv(kill_sflag, ircd.privileges.priv_srch, true,
                "Received KILL message for %s!%s@%s. From %s Path: %s",
                cp->nick, cp->user, cp->orighost, cli->nick, path);
        sendto_flag_priv(kill_sflag, ircd.privileges.priv_srch, false,
                "Received KILL message for %s!%s@%s. From %s Path: %s",
                cp->nick, cp->user, cp->host, cli->nick, path);

        /* okay, we've got the killpath, now construct the message to be sent
         * to remove the client */
        if (local)
            snprintf(msg, TOPICLEN, "Local kill by %s (%s)", cli->nick,
                    argv[2]);
        else {
            s = strchr(path, '(');
            snprintf(msg, TOPICLEN, "Killed (%s %s)", cli->nick,
                    (s == NULL ? "()" : s));
        }

        if (MYCLIENT(cp))
            /* let them know they were killed.. */
            sendto_one_from(cp, cli, NULL, "KILL", ":%s", msg);

        /* now, send the kill over the network if it was not a local kill.
         * if we had to chase, send it back the way it came with the new nick,
         * too. */
        if (!local) {
            sendto_serv_butone(sptr, cli, NULL, cp->nick, "KILL", ":%s", path);
            if (chasing)
                sendto_serv_from(sptr, cli, NULL, cp->nick, "KILL", ":%s",
                        path);
            cp->flags |= IRCD_CLIENT_KILLED;
        }

        destroy_client(cp, msg);
        if (cp == cli)
            return IRCD_CONNECTION_CLOSED; /* suicide kills end here. */

        if (++count == KILL_MAX) {
            sendto_one(cli, "NOTICE", ":Too many targets, kill list was "
                    "truncated.  Maximum is %d", KILL_MAX);
            break; /* stop! */
        }
    }

    return COMMAND_WEIGHT_NONE;
}

/* server kills are a bit easier because servers only ever send one kill at a
 * time.  In theory, this command will never be called with the origin being
 * us, so we only handle the remote case here.  This makes this all very
 * simple. :) */
SERVER_COMMAND(kill, 2, 2, 0) {
    client_t *cp;
    int chasing = 0;
    char msg[TOPICLEN + 1];
    char path[TOPICLEN + 1];
    char *s;

    cp = find_client(argv[1]);
    if (cp == NULL) {
        cp = client_get_history(argv[1], 0);
        if (cp == NULL) {
            sendto_serv(srv, RPL_FMT(sptr, ERR_NOSUCHNICK), argv[1]);
            return ERR_NOSUCHNICK;
        } 
        chasing = 1;
        /* don't notify servers about chasing .. */
    }

    strlcpy(path, argv[2], TOPICLEN + 1);

    /* okay, we've got the killpath, now construct the message to be sent
     * to remove the client */
    s = strchr(path, '(');
    snprintf(msg, TOPICLEN, "Killed (%s %s)", srv->name,
            (s == NULL ? "()" : s));

    /* send a notice about the kill */
    sendto_flag(servkill_sflag,
            "KILL message for %s!%s@%s. From %s Path: %s",
            cp->nick, cp->user, cp->host, srv->name, path);

    /* now, send the kill over the network.  if we had to chase, send it
     * back the way it came with the new nick, too. */
    sendto_serv_butone(sptr, NULL, srv, cp->nick, "KILL", ":%s", path);
    if (chasing)
        sendto_serv_from(sptr, NULL, srv, cp->nick, "KILL", ":%s", path);
    cp->flags |= IRCD_CLIENT_KILLED;
    destroy_client(cp, msg);

    return 0;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
