/*
 * flags.c: the FLAGS command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: flags.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

static struct {
    struct {
        int connect;
        int log;
        int spy;
    } privs;
    struct {
        int connect;
        int log;
        int skill;
        int spy;
    } flags;
} flags;

HOOK_FUNCTION(send_flag_log_hook);
HOOK_FUNCTION(send_flag_connect_hook);

MODULE_LOADER(flags) {
    int64_t i64 = 0;

    if (!get_module_savedata(savelist, "flags.privs", &flags.privs)) {
        flags.privs.connect = create_privilege("flag-connect",
                PRIVILEGE_FL_BOOL, &i64, NULL);
        flags.privs.log = create_privilege("flag-log", PRIVILEGE_FL_BOOL,
                &i64, NULL);
        flags.privs.spy = create_privilege("flag-spy", PRIVILEGE_FL_BOOL, &i64,
                NULL);
    }
    if (!get_module_savedata(savelist, "flags.flags", &flags.flags)) {
        flags.flags.connect = create_send_flag("CONNECT", SEND_LEVEL_OPERATOR,
                flags.privs.connect);
        flags.flags.log = create_send_flag("LOG", SEND_LEVEL_OPERATOR,
                flags.privs.log);
        flags.flags.spy = create_send_flag("SPY", SEND_LEVEL_OPERATOR,
                flags.privs.spy);
    }

    add_hook(me.events.log_debug, send_flag_log_hook);
    add_hook(me.events.log_notice, send_flag_log_hook);
    add_hook(me.events.log_warn, send_flag_log_hook);
    add_hook(me.events.log_error, send_flag_log_hook);
    add_hook(me.events.log_unknown, send_flag_log_hook);

    add_hook_before(ircd.events.client_connect, send_flag_connect_hook, NULL);
    add_hook_before(ircd.events.client_disconnect, send_flag_connect_hook,
            NULL);

    return 1;
}

MODULE_UNLOADER(flags) {

    if (reload) {
        add_module_savedata(savelist, "flags.privs", sizeof(flags.privs),
                &flags.privs);
        add_module_savedata(savelist, "flags.flags", sizeof(flags.flags),
                &flags.flags);
    } else {
        destroy_privilege(flags.privs.connect);
        destroy_privilege(flags.privs.log);
        destroy_send_flag(flags.flags.connect);
        destroy_send_flag(flags.flags.log);
        destroy_send_flag(flags.flags.skill);
    }

    remove_hook(me.events.log_debug, send_flag_log_hook);
    remove_hook(me.events.log_notice, send_flag_log_hook);
    remove_hook(me.events.log_warn, send_flag_log_hook);
    remove_hook(me.events.log_error, send_flag_log_hook);
    remove_hook(me.events.log_unknown, send_flag_log_hook);

    remove_hook(ircd.events.client_connect, send_flag_connect_hook);
    remove_hook(ircd.events.client_disconnect, send_flag_connect_hook);
}

/* this command allows operators to specify what kinds of messages they wish to
 * receive from the server.  The arguments should be a list of flags to set,
 * with either a + to set them, - to unset them, or no arguments to see what
 * flags they have set on them. */
CLIENT_COMMAND(flags, 0, 0, 0) {
    char buf[320];
    int len = 0;
    int i;

    if (!MYCLIENT(cli))
        return COMMAND_WEIGHT_NONE; /* um? */

    /* do sets if they sent arguments.. */
    if (argc > 1) {
        char *cur, *next;
        int lev;
        int plus = 1;
        int err;
        int oarg = 1;

        cur = argv[1];
        next = strchr(cur, ' ');
        while (cur != NULL) {
            if (next != NULL)
                *next++ = '\0';

            plus = 1;
            if (*cur == '+')
                cur++;
            else if (*cur == '-') {
                plus = 0;
                cur++;
            }

            if (!strcasecmp(cur, "?")) {
                /* list current flags */
                *buf = '\0';
                for (i = 0;i < ircd.sflag.size;i++) {
                    len += snprintf(buf + len, 320 - len, " %s",
                            ircd.sflag.flags[i].name);
                    if (len > 270) {
                        buf[len] = '\0';
                        sendto_one(cli, "NOTICE", ":Available flags:%s", buf);
                        len = 0;
                    }
                }
                if (len) {
                    buf[len] = '\0';
                    sendto_one(cli, "NOTICE", ":Available flags:%s", buf);
                    len = 0;
                }
                return COMMAND_WEIGHT_LOW; /* return immediately. */
            } else if (!strcasecmp(cur, "all")) {
                /* all is a special case .. */
                for (i = 0;i < ircd.sflag.size;i++) {
                    if (plus)
                        add_to_send_flag(i, cli, false);
                    else
                        remove_from_send_flag(i, cli, false);
                }
            } else {
                lev = find_send_flag(cur);
                if (lev < 0)
                    sendto_one(cli, "NOTICE", ":Unknown flag: %s", cur);
                else if (plus) {
                    if ((err = add_to_send_flag(lev, cli, false)) > 0)
                        sendto_one(cli, RPL_FMT(cli, err));
                } else
                    remove_from_send_flag(lev, cli, false);
            }

            if (next == NULL && argc > ++oarg)
                next = argv[oarg];
            if (next != NULL && *next) {
                cur = next;
                next = strchr(cur, ' ');
                continue;
            }
            break;
        }
    }

    /* whatever betide, show them their current flags. */
    *buf = '\0';
    for (i = 0;i < ircd.sflag.size;i++) {
        if (in_send_flag(i, cli)) {
            len += snprintf(buf + len, 320 - len, " %s",
                    ircd.sflag.flags[i].name);
            if (len > 270) {
                buf[len] = '\0';
                sendto_one(cli, "NOTICE", ":Current flags:%s", buf);
                len = 0;
            }
        }
    }
    if (len) {
        buf[len] = '\0';
        sendto_one(cli, "NOTICE", ":Current flags:%s", buf);
    } else if (*buf == '\0')
        sendto_one(cli, "NOTICE", ":No flags set.");

    return COMMAND_WEIGHT_MEDIUM;
}

HOOK_FUNCTION(send_flag_log_hook) {
    struct log_event_data *ldp = (struct log_event_data *)data;
    client_t fakecli;

    strcpy(fakecli.nick, log_conv_str(ldp->level));
    *fakecli.nick = toupper(*fakecli.nick);

    sendto_flag_from(flags.flags.log, &fakecli, NULL, "Log", "%s%s%s",
            ldp->module, (*ldp->module != '\0' ? ": " : ""), ldp->msg);

    return NULL;
}

/* this function just sends a notification message when clients connect or
 * disconnect. */
HOOK_FUNCTION(send_flag_connect_hook) {
    client_t *cli = (client_t *)data;
    
    if (cli->conn == NULL)
        return NULL;

    if (ep == ircd.events.client_connect)
        sendto_flag(flags.flags.connect,
                "Client connecting: %s (%s@%s) [%s] (%s)", cli->nick,
                cli->user, cli->orighost, cli->ip, cli->conn->cls->name);
    else
        sendto_flag(flags.flags.connect,
                "Client exiting: %s (%s@%s) [%s] [%s]", cli->nick, cli->user,
                cli->orighost, cli->ip, "Client Quit");

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
