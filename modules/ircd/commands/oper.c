/*
 * oper.c: the OPER command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "commands/mode.h"

IDSTRING(rcsid, "$Id: oper.c 751 2006-06-23 01:43:45Z wd $");

MODULE_REGISTER("$Rev: 751 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/commands/mode
*/

static XINFO_FUNC(xinfo_oper_func);

MODULE_LOADER(oper) {

    add_xinfo_handler(xinfo_oper_func, "OPERATORS", 0,
            "Provides information about server operators");

#define RPL_YOUREOPER 381
    CMSG("381", ":You are now an IRC operator");
#define ERR_NOOPERHOST 491
    CMSG("491", ":No O:lines for your host");

    return 1;
}
MODULE_UNLOADER(oper) {

    remove_xinfo_handler(xinfo_oper_func);

    DMSG(RPL_YOUREOPER);
    DMSG(ERR_NOOPERHOST);
}

/* argv[1] = operator nickname */
/* argv[2] = operator passowrd (unencrypted) */
CLIENT_COMMAND(oper, 2, 2, COMMAND_FL_REGISTERED) {
    conf_entry_t *cep = conf_find("operator", argv[1], CONF_TYPE_LIST,
            *ircd.confhead, 1);
    conf_list_t *clp;
    privilege_set_t *psp;
    int hostok = 0;
    char *ent, *s;
    char md5sum[33];
    char modes[128];
    char *fakeargv[3];

    if (cep == NULL) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOOPERHOST));
        return COMMAND_WEIGHT_MEDIUM;
    }

    clp = cep->list;
    /* now run a host check.  this is somewhat complicated, we have to walk
     * through each 'host' entry which will be of the form [user@]host.  If
     * there is a user@ portion it must match the *IDENT* given.  the host
     * portion is both hostmatch()ed and ipmatch()ed.  we do this first so that
     * people who don't match can't guess at the password. */
    ent = conf_find_entry("host", clp, 1);
    if ((ent = conf_find_entry("host", clp, 1)) == NULL &&
            (ent = conf_find_entry("host-list", clp, 1)) == NULL) {
        /* this is a poorly configured O:line, gripe about it */
        log_warn("operator %s defined with no host data", cep->string);
        sendto_one(cli, RPL_FMT(cli, ERR_NOOPERHOST));
        return COMMAND_WEIGHT_MEDIUM;
    }

    /* do host checks.  this consists of individual 'host' entries and
     * 'host-lists'.  try hosts first, then host-lists.  we use this handy
     * macro here to do the test. */
#define OPER_CHECK_HOST(_host) do {                                        \
    if ((s = strchr(_host, '@')) != NULL) {                                \
        if (strncasecmp(_host, cli->user, s - _host))                        \
            continue;                                                        \
        s++; /* moving on */                                                \
    } else                                                                \
        s = _host;                                                        \
    /* usernames match or aren't there */                                \
    if (hostmatch(s, cli->conn->host) || ipmatch(s, cli->ip)) {                \
        hostok = 1;                                                        \
        break; /* we have a weiner */                                        \
    }                                                                        \
} while (0)

    ent = NULL; /* start afresh */
    while (!hostok &&
            (ent = conf_find_entry_next("host", ent, clp, 1)) != NULL)
        OPER_CHECK_HOST(ent);
    if (!hostok) {
        while (!hostok &&
                (ent = conf_find_entry_next("host-list", ent, clp, 1)) !=
                NULL) {
            hostlist_t *hlp = find_host_list(ent);
            int i;

            if (hlp == NULL) {
                log_warn("host-list %s does not exist", ent);
                continue;
            }
            for (i = 0;i < hlp->entries;i++)
                OPER_CHECK_HOST(hlp->list[i]);
        }
    }
    if (!hostok) {
        /* we didn't find anything */
        sendto_one(cli, RPL_FMT(cli, ERR_NOOPERHOST));
        sendto_flag(ircd.sflag.ops, "Failed OPER attempt by %s (%s@%s)",
                cli->nick, cli->user, cli->orighost);
        return COMMAND_WEIGHT_MEDIUM;
    }

    /* now check the password */
    ent = conf_find_entry("pass", clp, 1);
    if (ent == NULL) {
        /* this is a poorly configured O:line, gripe about it */
        log_warn("operator %s defined with no password data", cep->string);
        sendto_one(cli, RPL_FMT(cli, ERR_NOOPERHOST));
        return COMMAND_WEIGHT_NONE;
    }
    md5_data(argv[2], strlen(argv[2]), md5sum);
    if (strcmp(md5sum, ent)) {
        sendto_one(cli, RPL_FMT(cli, ERR_PASSWDMISMATCH));
        sendto_flag(ircd.sflag.ops, "Failed OPER attempt by %s (%s@%s)",
                cli->nick, cli->user, cli->orighost);
        return COMMAND_WEIGHT_MEDIUM;
    }

    /* they have access, but we still need to check some configuration stuff
     * before we are sure they'll be opered. */

    /* their connection class.. */
    ent = conf_find_entry("class", clp, 1);
    if (ent != NULL) {
        class_t *cls = find_class(ent);
        if (cls != NULL) {
            add_to_class(cls, cli->conn);
        } else
            log_warn("operator %s defined with nonexistant class %s",
                    cep->string, ent);
    }
    /* privilege set AFTER class */
    psp = ircd.privileges.oper_set;
    if ((ent = conf_find_entry("privilege-set", clp, 1)) != NULL) {
        if ((psp = find_privilege_set(ent)) == NULL) {
            log_warn("operator %s defined with nonexistant privilege set %s",
                    cep->string, ent);
            psp = ircd.privileges.oper_set;
        }
    }
    if (psp == NULL) {
        log_warn("operator %s has no appropriate privilege set.", cep->string);
        sendto_one(cli, RPL_FMT(cli, ERR_NOOPERHOST));
        return COMMAND_WEIGHT_MEDIUM;
    }
    cli->pset = psp;
    if (!BPRIV(cli, ircd.privileges.priv_operator)) {
        log_warn("operator %s defined with non-oper privilege set %s",
                cep->string, ent);
        sendto_one(cli, RPL_FMT(cli, ERR_NOOPERHOST));
        return COMMAND_WEIGHT_MEDIUM;
    }

    /* from this point success is gauranteed. */
    /* message set too */
    ent = conf_find_entry("message-set", clp, 1);
    if (ent != NULL) {
        message_set_t *msp = find_message_set(ent);
        if (msp != NULL)
            cli->conn->mset = msp;
        else
            log_warn("operator %s defined with nonexistant message set %s",
                    cep->string, ent);
    }

    /* now, set them plus-oh-boy. */
    ent = conf_find_entry("modes", clp, 1);
    if (ent != NULL)
        sprintf(modes, "+o%s", ent);
    else
        strcpy(modes, "+os");
    fakeargv[0] = modes;
    user_mode(cli, cli, 1, fakeargv, 1);

    /* look for flags, too */
    if ((ent = conf_find_entry("flags", clp, 1)) != NULL) {
        fakeargv[0] = "FLAGS";
        fakeargv[1] = "+KILLS"; /* defaults here.. */
        fakeargv[2] = strdup(ent);
        command_exec_client(3, fakeargv, cli);
        free(fakeargv[2]);
    }

    /* TADA! */
    sendto_flag(ircd.sflag.ops, "%s (%s!%s@%s) is now operator (O)",
            cep->string, cli->nick, cli->user, cli->orighost);
    sendto_one(cli, RPL_FMT(cli, RPL_YOUREOPER));

    return COMMAND_WEIGHT_NONE;
}

/* this handler provides information on currently active operators, as well as
 * all operators configured on the server.  if called with no arguments it only
 * lists configured opers, if called with an argument it will list all
 * operators which match that argument */
static XINFO_FUNC(xinfo_oper_func) {
    char rpl[XINFO_LEN];
    struct chanlink *clp;
    conf_entry_t *cep;

    /* send them a list of all active opers and configured opers on this
     * server. */
    LIST_FOREACH(clp, &ircd.sflag.flags[ircd.sflag.ops].users, lpchan) {
        snprintf(rpl, XINFO_LEN, "%s IDLE %d", clp->cli->nick,
                me.now - clp->cli->last);
        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "ACTIVE", rpl);
    }

    if (argc < 2)
        return; /* nothing else to do here.. */
    /* now for the configured ops.. */
    cep = NULL;
    while ((cep = conf_find_next("operator", NULL, CONF_TYPE_LIST, cep,
                    *ircd.confhead, 1)) != NULL) {
        char *cls, *priv;

        if (cep->string == NULL || !match(argv[1], cep->string))
            continue; /* skip it.. */
        snprintf(rpl, XINFO_LEN, "%s CLASS %s PRIVILEGES %s", cep->string,
                ((cls = conf_find_entry("class", cep->list, 1)) != NULL ? cls :
                 "<default>"),
                ((priv = conf_find_entry("privilege-set", cep->list, 1))
                 != NULL ? priv : "default-oper"));
        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "OPERATOR", rpl);
    }
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
