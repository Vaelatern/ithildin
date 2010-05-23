/*
 * command.c: command structure management/parsing
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file provides methods for adding command modules to the server, as well
 * as handling commands issued from protocol modules.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: command.c 751 2006-06-23 01:43:45Z wd $");

static int command_exec_numeric(int, char **, server_t *);

/* add_command adds a new command to the command hash and list. naaarf. */
int add_command(char *name) {
    char mname[PATH_MAX];
    char fn_name[33]; /* assume 32 character functions. */
    struct command *cmd;
    module_t *dll;
    char *s;
    int *ip;
    uint64_t i64;
    conf_list_t *clp = NULL;

    if (name == NULL || *name == '\0')
        return 0;

    /* only do all this stuff if the command doesn't exist. */
    if ((cmd = find_command(name)) == NULL || cmd->flags & COMMAND_FL_ALIAS) {
        sprintf(mname, "ircd/commands/%s", name);

        /* see if this is an alias, if it is warn the user that it is about to
         * get trampled (don't call this function to add aliases! :) */
        if (cmd != NULL) {
            log_warn("overwriting alias %s for command %s", cmd->name,
                    ((struct command *)cmd->dll)->name);
            remove_command(cmd->name);
        }

        /* see if it's loaded, and try to load it if it isn't. */
        dll = find_module(mname);
        if (dll == NULL &&
                !load_module(mname, MODULE_FL_CREATE|MODULE_FL_QUIET)) {
            log_error("unable to load module for command %s", name);
            return 0;
        }

        /* if it didn't exist before, do some init stuff */
        if (dll == NULL)
            dll = find_module(mname);

        cmd = malloc(sizeof(struct command));
        memset(cmd, 0, sizeof(struct command));

        cmd->dll = dll;

        strlcpy(cmd->name, name, COMMAND_MAXLEN + 1);

#define CMD_GRAB_SYMBOL_INTO(type, sym, into) do {                            \
    sprintf(fn_name, #type "_%s_" #sym, name);                                \
    into = module_symbol(dll, fn_name);                                       \
} while (0)
        CMD_GRAB_SYMBOL_INTO(c, cmd, cmd->client.cmd);
        CMD_GRAB_SYMBOL_INTO(c, min, ip);
        cmd->client.min = (ip == NULL ? 0 : *ip);
        CMD_GRAB_SYMBOL_INTO(c, max, ip);
        cmd->client.max = (ip == NULL ? 0 : *ip);
        CMD_GRAB_SYMBOL_INTO(c, flags, ip);
        cmd->client.flags = (ip == NULL ? 0 : *ip);
        cmd->client.ev = NULL;

        CMD_GRAB_SYMBOL_INTO(s, cmd, cmd->server.cmd);
        CMD_GRAB_SYMBOL_INTO(s, min, ip);
        cmd->server.min = (ip == NULL ? 0 : *ip);
        CMD_GRAB_SYMBOL_INTO(s, max, ip);
        cmd->server.max = (ip == NULL ? 0 : *ip);
        CMD_GRAB_SYMBOL_INTO(s, flags, ip);
        cmd->server.flags = (ip == NULL ? 0 : *ip);
        cmd->server.ev = NULL;

        if (cmd->client.cmd == NULL && cmd->server.cmd == NULL) {
            free(cmd);
            return 0;
        }

        /* create a privilege for the command, called 'command-<name>', default
         * it to yes, but of course allow it to be set otherwise. */
        i64 = 1;
        sprintf(mname, "command-%s", cmd->name);
        cmd->priv = create_privilege(mname, PRIVILEGE_FL_BOOL, &i64, NULL);

        /* insert the command in the hash .. */
        hash_insert(ircd.hashes.command, cmd);
        /* and the command list */
        LIST_INSERT_HEAD(ircd.lists.commands, cmd, lp);
    }

    if ((clp = conf_find_list("commands", *ircd.confhead, 1)) != NULL) {
        conf_entry_t *cep;

        cep = conf_find("command", cmd->name, CONF_TYPE_LIST, clp, 1);
        if (cep != NULL)
            clp = cep->list;
        else
            clp = NULL;
    }

    /* grab possible conf settings */
    if (clp != NULL) {
        s = conf_find_entry("weight", clp, 1);
        if (s != NULL) {
            /* try to see if they wanted to specify it in words instead of
             * numbers */
            if (!strcasecmp(s, "none"))
                cmd->weight = COMMAND_WEIGHT_NONE;
            else if (!strcasecmp(s, "low"))
                cmd->weight = COMMAND_WEIGHT_LOW;
            else if (!strcasecmp(s, "medium"))
                cmd->weight = COMMAND_WEIGHT_MEDIUM;
            else if (!strcasecmp(s, "high"))
                cmd->weight = COMMAND_WEIGHT_HIGH;
            else if (!strcasecmp(s, "extreme"))
                cmd->weight = COMMAND_WEIGHT_EXTREME;
            else
                cmd->weight = str_conv_int(s, COMMAND_WEIGHT_NONE);
        }

        /* grok 'aliases' */
        s = NULL;
        while ((s = conf_find_entry_next("alias", s, clp, 1)) != NULL)
            add_command_alias(cmd->name, s);
    }
    cmd->conf = clp; /* and store this here. */
    return 1;
}

void add_command_alias(char *cmdname, char *alias) {
    struct command *cmd = find_command(cmdname);
    struct command *acmd = find_command(alias);

    if (acmd != NULL) {
        /* hrm.  look at it.  if it's an alias for *our* command then
         * just continue on.  if it's something else, yell loudly. */
        if (acmd->flags & COMMAND_FL_ALIAS) {
            if ((struct command *)acmd->dll != cmd)
                log_warn("alias %s for command %s would overwrite "
                        "alias for command %s", alias, cmd->name,
                        ((struct command *)acmd->dll)->name);
        } else
            log_warn("alias %s for command %s would overwrite "
                    "command %s", alias, cmd->name, acmd->name);

        return;
    }

    /* insert the command otherwise */
    acmd = malloc(sizeof(struct command));
    memset(acmd, 0, sizeof(struct command));
    strlcpy(acmd->name, alias, COMMAND_MAXLEN + 1);
    acmd->dll = (module_t *)cmd;
    acmd->flags = COMMAND_FL_ALIAS;
    hash_insert(ircd.hashes.command, acmd);
    LIST_INSERT_HEAD(ircd.lists.commands, acmd, lp);
    log_debug("added alias %s for command %s", acmd->name, cmd->name);
}

/* conveniently, this function can unload the module if need-be.  if we get
 * called from the unload hook, we delete ourselves from the command hash
 * before calling unload again, so even if this function is re-called it will
 * not proceed to try and unload recursively! */
void remove_command(char *name) {
    struct command *cmd = find_command(name);

    if (cmd == NULL)
        return; /* hmpf. */

    /* remove it from the hash */
    hash_delete(ircd.hashes.command, cmd);
    /* and the list */
    LIST_REMOVE(cmd, lp);

    /* if the command *isn't* an alias we do all this other stuff. */
    if (!(cmd->flags & COMMAND_FL_ALIAS)) {
        struct command *acmd, *acmd2;

        /* find all the aliases for this command and remove them, too */
        acmd = LIST_FIRST(ircd.lists.commands);
        while (acmd != NULL) {
            acmd2 = LIST_NEXT(acmd, lp);
            if (acmd->flags & COMMAND_FL_ALIAS &&
                    acmd->dll == (module_t *)cmd)
                remove_command(acmd->name);
            acmd = acmd2;
        }

        /* remove the automatically created privilege ... */
        if (cmd->priv > -1) {
            destroy_privilege(cmd->priv);
            cmd->priv = -1;
        }

        /* and unload the command module */
        if (cmd->dll != NULL)
            unload_module(cmd->dll->name);
        cmd->dll = NULL;
    }
    free(cmd);
}

struct command *find_command(char *name) {
    return (struct command *)hash_find(ircd.hashes.command, name);
}

int pass_command(client_t *cli, server_t *srv, char *cmd, char *fmt, int argc,
        char **argv, int argsrv) {
    server_t *sp; /* this will be the server we want to send to. */

    /* if there is no server argument, or the server argument matches our name,
     * it is directed at us. */
    if (argc <= argsrv || match(argv[argsrv], ircd.me->name))
        return COMMAND_PASS_LOCAL;

    /* okay.  yuck.  wildcards are only available for hunting servers, not
     * users (to prevent denial of service type attacks by utilizing heavy CPU)
     * that being the case, if argv[argvsrv] is a wildcard string, assume we're
     * looking for a server and simply call find_server.  if the resultant
     * server seems to be in the same direction as the message came from, drop
     * the message. */
    if (strchr(argv[argsrv], '*') != NULL ||
            strchr(argv[argsrv], '?') != NULL) {
        sp = find_server(argv[argsrv]);
        if (sp != NULL && srv_uplink(sp) == sptr->conn)
            return COMMAND_PASS_NONE; /* wrong direction. */
    } else {
        sp = find_server(argv[argsrv]);
        if (sp == NULL) {
            client_t *cp; /* no luck, try finding it as a client.. */
            cp = find_client(argv[argsrv]);
            if (cp != NULL)
                sp = cp->server;
        }
    }

    /* okay, we've either tracked down our server or haven't found anything to
     * send to,  decide based on this. */
    if (sp == NULL) {
        if (cli != NULL) {
            sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHSERVER), argv[argsrv]);
            return COMMAND_PASS_NONE;
        } else {
            sendto_serv(srv, RPL_FMT(srv, ERR_NOSUCHSERVER), argv[argsrv]);
            return COMMAND_PASS_NONE;
        }
    }

    if (sp == ircd.me)
        return COMMAND_PASS_LOCAL; /* oh, hey, it's us after all. */

    sendto_serv_from(sp, cli, srv, NULL, cmd, fmt, argv[1], argv[2], argv[3],
            argv[4], argv[5], argv[6], argv[7], argv[8], argv[9]);
    return COMMAND_PASS_REMOTE;
}



int command_add_hook(char *name, int client, hook_function_t hook,
        int flags) {
    struct command *cmd = find_command(name);

    if (cmd != NULL && cmd->flags & COMMAND_FL_ALIAS)
        cmd = (struct command *)cmd->dll;
    else if (cmd == NULL)
        return 0;

    flags |= COMMAND_FL_HOOKED; /* tweak here just in case */

    if (client) {
        if (cmd->client.cmd == NULL)
            return 0;
        if (cmd->client.ev == NULL) {
            cmd->client.ev = create_event(EVENT_FL_NORETURN);
            if (cmd->client.ev == NULL)
                return 0;
        }
        cmd->client.flags |= flags;
        add_hook(cmd->client.ev, hook);
    } else {
        if (cmd->server.cmd == NULL)
            return 0;
        if (cmd->server.ev == NULL) {
            cmd->server.ev = create_event(EVENT_FL_NORETURN);
            if (cmd->server.ev == NULL)
                return 0;
        }
        cmd->server.flags |= flags;
        add_hook(cmd->server.ev, hook);
    }

    return 1;
}

void command_remove_hook(char *name, int client, hook_function_t hook) {
    struct command *cmd = find_command(name);

    if (cmd != NULL && cmd->flags & COMMAND_FL_ALIAS)
        cmd = (struct command *)cmd->dll;
    else if (cmd == NULL)
        return;

    if (client) {
        if (cmd->client.cmd == NULL)
            return;
        remove_hook(cmd->client.ev, hook);
        if (EVENT_HOOK_COUNT(cmd->client.ev) == 0) {
            destroy_event(cmd->client.ev);
            cmd->client.flags &= ~(COMMAND_FL_HOOKED | COMMAND_FL_EXCL_HOOK);
        }
    } else {
        if (cmd->server.cmd == NULL)
            return;
        remove_hook(cmd->server.ev, hook);
        if (EVENT_HOOK_COUNT(cmd->server.ev) == 0) {
            destroy_event(cmd->server.ev);
            cmd->server.flags &= ~(COMMAND_FL_HOOKED | COMMAND_FL_EXCL_HOOK);
        }
    }

    return;
}

int command_exec_client(int argc, char **argv, client_t *cli) {
    struct command *cmd = find_command(argv[0]);
    int ret, delta, flimit;
    time_t idle = 0; /* time client was idle for */
    bool ours; /* we need to track this independent of cli because cli might go
                  away at any time once a command begins executing. */

    /* sanity check what we're getting first.  if sptr is not what we think the
     * uplink for the client is then treat this as a 'fake direction' error and
     * simply ignore the message.  this can happen during netjoins with
     * nickname collisions so we don't get too nasty about it.. */
    if (sptr != cli_server_uplink(cli)) {
        log_debug("fake direction: %s passed us a command (%s) for %s who "
                "should come from %s", sptr->name, argv[0], cli->nick,
                ((server_t *)cli_server_uplink(cli))->name);
        return 0;
    }

    if (cmd != NULL && cmd->flags & COMMAND_FL_ALIAS)
        cmd = (struct command *)cmd->dll;

    if (cmd == NULL || cmd->client.cmd == NULL) {
        sendto_one(cli, RPL_FMT(cli, ERR_UNKNOWNCOMMAND), argv[0]);
        return ERR_UNKNOWNCOMMAND;
    }

    /* check for minimal argument match */
    if (argc - 1 < cmd->client.min) {
        if (MYCLIENT(cli))
            sendto_one(cli, RPL_FMT(cli, ERR_NEEDMOREPARAMS), argv[0]);
        return ERR_NEEDMOREPARAMS;
    } else if (argc - 1 > cmd->client.max &&
            cmd->client.flags & COMMAND_FL_FOLDMAX) {
        /* fold the extras into the final argv item */
        int i;
        char *s = argv[cmd->client.max];
        int len = strlen(s);

        s += len;
        for (i = cmd->client.max + 1;i < argc;i++) {
            *s++ = ' ';
            len++;
            strncpy(s, argv[i], COMMAND_MAXARGLEN - len);
            len += strlen(s);
            s += strlen(s);
        }
        argv[cmd->client.max][COMMAND_MAXARGLEN] = '\0';
    }

    /* do some checks for local clients to make sure they can use commands. */
    if (MYCLIENT(cli)) {
        /* if the command isn't specifically for unregistered clients, asssume
         * the client must be registered to use it. */
        if (!CLIENT_REGISTERED(cli)) {
            if (!(cmd->client.flags & COMMAND_FL_UNREGISTERED)) {
                sendto_one(cli, RPL_FMT(cli, ERR_NOTREGISTERED));
                return ERR_NOTREGISTERED;
            }
        } else {
            /* if a registered client tries to use a command ONLY for
             * unregistered clients, yell. */
            if (cmd->client.flags & COMMAND_FL_UNREGISTERED &&
                    !(cmd->client.flags & COMMAND_FL_REGISTERED)) {
                sendto_one(cli, RPL_FMT(cli, ERR_ALREADYREGISTERED));
                return ERR_ALREADYREGISTERED;
            }
        }
        /* check operator and command use privileges.  Why do this only for
         * local clients?  Because we need to assume that even if we are not
         * configured to accept commands from a client, every other server
         * is.  Without a distributed privilege understanding we have to
         * assume positive intent. */
        if ((cmd->client.flags & COMMAND_FL_OPERATOR && !OPER(cli)) ||
                !BPRIV(cli, cmd->priv)) {
            sendto_one(cli, RPL_FMT(cli, ERR_NOPRIVILEGES));
            return ERR_NOPRIVILEGES;
        }
    }
                
    if (cmd->client.flags & COMMAND_FL_HOOKED) {
        struct command_hook_args hdata = {argc, argv, cli, NULL};
        hook_event(cmd->client.ev, &hdata);
        if (cmd->client.flags & COMMAND_FL_EXCL_HOOK)
            return 0; /* if this is an 'exclusive' hook. */
    }

    ours = MYCLIENT(cli); /* cache this now */
    if (cli->conn != NULL) {
        idle = me.now - cli->conn->last;
        cli->conn->last = me.now;
    }

    /* for some values of ret we need to not ever do the stuff below because
     * 'cli' could have gone away from us, and we need to be careful not to
     * touch it in this state. */
    switch ((ret = cmd->client.cmd(cmd, argc, argv, cli))) {
    case IRCD_CONNECTION_CLOSED:
    case IRCD_PROTOCOL_CHANGED:
        return ret;
    }

    /* same as above, if it's not ours we need to get out of here now.
     * commands that destroy non-local clients are not under obligation to
     * return IRCD_CONNECTION_CLOSED  for those clients, so we don't know
     * that the client structure is dead, and we're done handling it at any
     * rate. */
    if (ours == false || cli->conn == NULL || cli->conn->cls->flood <= 0)
        return ret;

    /* flood detection code:
     * You can flood up to whatever your class says is too high.  If the class
     * says 'flood' is <= 0 then you never hit this code.  For every second
     * your client is idle your flood is reduced by 1/8th of the maximum flood
     * limit of the class (so if you idle for 8 or more seconds your flood hits
     * 0). */
    if (me.now - cli->signon < COMMAND_WEIGHT_SIGNON_GRACE)
        /* Special dispensation for new clients.  Give them 1.5x the normal
         * limit for the first COMMAND_WEIGHT_SIGNON_GRACE seconds of their
         * connection. */
        flimit = cli->conn->cls->flood * 1.5;
    else
        flimit = cli->conn->cls->flood;

    if (idle > COMMAND_WEIGHT_REDUCE_FACTOR)
        cli->conn->flood = 0;
    else if (idle) {
        delta = (flimit / COMMAND_WEIGHT_REDUCE_FACTOR) * idle;
        if (cli->conn->flood < delta)
            cli->conn->flood = 0;
        else
            cli->conn->flood -= delta;
    }

    if (ret > 0) {
        if (ret > COMMAND_WEIGHT_MAX)
            ret = COMMAND_WEIGHT_MAX;

        /* XXX: do we want to let people add additional configured weight to
         * each command?  the placeholders are in the structures, and only the
         * code would need to be added.  I don't know, yet, so this code is
         * commented out.. */
#if 0
        ret += cmd->weight;
#endif
        cli->conn->flood += ret;
        if (cli->conn->flood >= flimit) {
            destroy_client(cli, "Excess Flood");
            return IRCD_CONNECTION_CLOSED;
        }
    }

    return ret;
}

int command_exec_server(int argc, char **argv, server_t *srv) {
    struct command *cmd = find_command(argv[0]);
    int i, len;
    char *s;

    /* check for fake directions, just like above. */
    if (sptr != srv_server_uplink(srv)) {
        log_debug("fake direction: %s passed us a command (%s) for %s which "
                "should come from %s", sptr->name, argv[0], srv->name,
                ((server_t *)srv_server_uplink(srv))->name);
        return 0;
    }

    if (cmd != NULL && cmd->flags & COMMAND_FL_ALIAS)
        cmd = (struct command *)cmd->dll;

    if (cmd == NULL || cmd->server.cmd == NULL) {
        /* see if it's a numeric. */
        if (argv[0][3] == '\0' && isdigit(argv[0][0]) && isdigit(argv[0][1]) &&
                isdigit(argv[0][2]))
            return command_exec_numeric(argc, argv, srv);

        log_warn("received unknown command %s from server %s", argv[0],
                srv->name);
        return ERR_UNKNOWNCOMMAND;
    }

    /* check for minimal argument match */
    if (argc - 1 < cmd->server.min) {
        log_warn("command %s from server %s did not have enough arguments",
                argv[0], srv->name);
        return ERR_NEEDMOREPARAMS;
    } else if (argc - 1 > cmd->server.max &&
            cmd->client.flags & COMMAND_FL_FOLDMAX) {
        /* fold the extras into the final argv item */
        s = argv[cmd->server.max];
        len = strlen(s);

        s += len;
        for (i = cmd->server.max + 1;i < argc;i++) {
            *s++ = ' ';
            len++;
            strncpy(s, argv[i], COMMAND_MAXARGLEN - len);
            len += strlen(s);
            s += strlen(s);
        }
        argv[cmd->server.max][COMMAND_MAXARGLEN] = '\0';
    }

    if (MYSERVER(srv)) {
        srv->conn->last = me.now;
        if (!SERVER_REGISTERED(srv))
            if (!(cmd->server.flags & COMMAND_FL_UNREGISTERED)) {
                log_debug("dropping command %s from unregistered server",
                        argv[0]);
                return ERR_NOTREGISTERED;
            }
    }
    if (cmd->server.flags & COMMAND_FL_HOOKED) {
        struct command_hook_args hdata = {argc, argv, NULL, srv};
        hook_event(cmd->server.ev, &hdata);
        if (cmd->server.flags & COMMAND_FL_EXCL_HOOK)
            return 0; /* if this is an 'exclusive' hook. */
    }
    return cmd->server.cmd(cmd, argc, argv, srv);
}

static int command_exec_numeric(int argc, char **argv, server_t *srv) {
    /* handle numeric routing specially.  argv[1] is our destination,
     * it may be either a client or a server.  It's usually going to be
     * a client, so check that.  If not client, then server.  If
     * nothing is found we don't error back.  Should we?  Anyways, if
     * we find what we're after, we have to re-construct the string to
     * pass it along.  Yech. */
    client_t *cp;
    server_t *sp;
    /* make the buffer ridiculously large, just in case */
    char numarg[COMMAND_MAXARGLEN * COMMAND_MAXARGS + 1];
    char *s = numarg;
    int len = 0;
    int i;

    *s = '\0';
    /* first, rebuild the buffer.  fold in everything to argv[argc] into
     * numarg.  Also, make sure the last (or only?) argument is : prefixed
     * (just in case.. heh).  And use that to send.  We hope we don't trounce
     * our buffers. ;) */
    for (i = 2;i < argc;i++) {
        /* if this is the last argument, add a : in. */
        if (i == (argc - 1)) {
            *s++ = ':';
            len++;
        }
        len += strlcpy(s, argv[i], COMMAND_MAXARGLEN);
        s = numarg + len;
        *s++ = ' ';
        len++;
    }
    if (*numarg == '\0') {
        numarg[0] = ':';
        numarg[1] = '\0';
    } else
        *(s - 1) = '\0';

    cp = find_client(argv[1]);
    if (cp != NULL) {
        sendto_one_from(cp, NULL, srv, argv[0], "%s", numarg);
        return 0;
    }
    sp = find_server(argv[1]);
    if (sp != NULL) {
        sendto_serv_from(sp, NULL, srv, sp->name, argv[0], "%s",
                numarg);
        return 0;
    }

    /* no luck, give up.. */
    return ERR_NOSUCHSERVER;
}

/* this function reduces a string list into a list of unique items.  'sep' is
 * the token(s) used to separate the list entries.  the first one will be used
 * in the regnerated list. */
char *reduce_string_list(char *list, char *sep) {
    static char buf[COMMAND_MAXARGLEN + 1], *bptr;
    int len = 0;
    char *cur, *t;

    *buf = '\0';
    while ((cur = strsep(&list, sep)) != NULL) {
        if (*cur == '\0')
            continue; /* skip empty entries. */
        /* okay, try and find it in buf (sigh). */
        bptr = buf;
        while ((t = strsep(&bptr, sep)) != NULL) {
            if (*t == '\0')
                continue; /* for empty lists.. */
            if (!strcasecmp(t, cur)) {
                *(bptr - 1) = *sep;
                break; /* already in the list */
            } else if (bptr != NULL)
                *(bptr - 1) = *sep; /* un-nullify. */
        }
        if (t == NULL) {
            /* entry not found */
            len = strlcat(buf, cur, COMMAND_MAXARGLEN);
            buf[len++] = *sep;
            buf[len] = '\0';
        }
    }
    buf[len - 1] = '\0'; /* remove the last bit from sep */
    return buf;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
