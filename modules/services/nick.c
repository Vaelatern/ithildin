/*
 * nick.c: nickname service routines
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains the handling routines for the nickname service.
 */

#include <ithildin/stand.h>

#include "services.h"

IDSTRING(rcsid, "$Id: nick.c 579 2005-08-21 06:38:18Z wd $");

/* The various commands we handle */
static SERVICES_COMMAND(ns_access);
static SERVICES_COMMAND(ns_auth);
static SERVICES_COMMAND(ns_drop);
static SERVICES_COMMAND(ns_identify);
static SERVICES_COMMAND(ns_info);
static SERVICES_COMMAND(ns_register);
static SERVICES_COMMAND(ns_set);

/* this is a simple function to allocate a new registred nickname entry with
 * the given name.  It allocates, names, and links the structure in with the
 * necessary lists. */
regnick_t *regnick_create(char *name) {
    regnick_t *np = calloc(1, sizeof(regnick_t));

    strcpy(np->name, name);

    LIST_INSERT_HEAD(&services.db.list.nicks, np, lp);
    hash_insert(services.db.hash.nick, np);

    return np;
}

/* This function destroys a registered nick.  It clears its memory, clears it
 * from the lists, and also destroys any children nicknames it may have. */
void regnick_destroy(regnick_t *np) {
    regnick_t *np2;

    if (np->email != NULL) {
        if (--np->email->nicks == 0)
            destroy_mail_contact(np->email);
    }

    while ((np2 = LIST_FIRST(&np->links)) != NULL)
        regnick_destroy(np2);

    if (np->parent != NULL)
        LIST_REMOVE(np, linklp);
    LIST_REMOVE(np, lp);
    hash_delete(services.db.hash.nick, np);
    free(np);
}

struct regnick_access *regnick_access_add(regnick_t *np, char *mask) {
    struct regnick_access *rap = NULL;
    size_t asize;
    char *user, *host;

    asize = sizeof(struct regnick_access);
    if ((host = strchr(mask, '@')) == NULL) {
        host = mask;
        user = "*";
    } else {
        *host++ = '\0';
        user = mask;
    }
    asize += strlen(host);
    rap = malloc(asize);
    rap->size = asize;
    strlcpy(rap->user, user, USERLEN + 1);
    if (user == mask)
        *(host - 1) = '@';
    strcpy(rap->host, host);

    SLIST_INSERT_HEAD(&np->alist, rap, lp);

    return rap;
}

struct regnick_access *regnick_access_find(regnick_t *np, char *mask) {
    struct regnick_access *rap;
    char rmask[USERLEN + HOSTLEN + 2];

    SLIST_FOREACH(rap, &np->alist, lp) {
        snprintf(rmask, USERLEN + HOSTLEN + 2, "%s@%s", rap->user, rap->host);
        if (!strcasecmp(mask, rmask))
            return rap;
    }

    return NULL;
}

void regnick_access_del(regnick_t *np, struct regnick_access *rap) {

    SLIST_REMOVE(&np->alist, rap, regnick_access, lp);
    free(rap);
}

/* this function checks to see if the client (cli) has access to the registered
 * nick (np) at 'level' or better.  it tries to optimize checks as much as
 * possible. */
int has_access_to_nick(client_t *cli, regnick_t *np, int64_t level) {
    struct regnick_access *rap;
    struct services_client_data *scdp =
        (struct services_client_data *)mdext(cli, services.mdext.client);

    if (level & NICK_IFL_IDACCESS) {
        if (scdp->nick != NULL && scdp->nick == np &&
                regnick_idaccess(scdp->nick))
            return 1;
        else if (!(level & NICK_IFL_MASKACCESS))
            return 0;
    }

    if (level & NICK_IFL_MASKACCESS) {
        if (scdp->nick != NULL && scdp->nick == np &&
                regnick_access(scdp->nick))
            return 1;
        SLIST_FOREACH(rap, &np->alist, lp) {
            if (match(rap->user, cli->user) &&
                    (match(rap->host, cli->host) ||
                     ipmatch(rap->host, cli->ip)))
                return 1;
        }
    }

    return 0;
}

/* This function does a variety of setup things.  It creates the nickname
 * service command hash table, and creates the various messages as well. */
enum reply_codes {
    RPL_SYNTAX = 0,
    RPL_HELP,
    RPL_NOTREG,
    RPL_NOACCESS,
    RPL_NOACCESS_ID,
    RPL_UNKNOWN_COMMAND,

    RPL_NOTYOURNICK,
    RPL_NICKISPROTECTED,

    RPL_ACCESS_ADD_NOBANG,
    RPL_ACCESS_ADD_BADMASK,
    RPL_ACCESS_ADD_TOOMANY,
    RPL_ACCESS_ADD_ALREADY,
    RPL_ACCESS_ADD,
    RPL_ACCESS_CHECK_OFFLINE,
    RPL_ACCESS_CHECK_NONE,
    RPL_ACCESS_CHECK_LIST,
    RPL_ACCESS_CHECK_IDENT,
    RPL_ACCESS_DEL_NOTFOUND,
    RPL_ACCESS_DEL,
    RPL_ACCESS_LIST_START,
    RPL_ACCESS_LIST_END,
    RPL_ACCESS_WIPE,

    RPL_AUTH_ALREADY,
    RPL_AUTH_BADKEY,
    RPL_AUTH_FINISHED,

    RPL_DROP_OK,

    RPL_IDENTIFY_BADPASS,
    RPL_IDENTIFY_OK,

    RPL_INFO_START,
    RPL_INFO_ONLINE,
    RPL_INFO_NOTACTIVATED,
    RPL_INFO_LASTADDR,
    RPL_INFO_PARENT,
    RPL_INFO_LASTTIME,
    RPL_INFO_REGTIME,
    RPL_INFO_TIMENOW,
    RPL_INFO_ADMIN,
    RPL_INFO_EMAIL,
    RPL_INFO_PROTECT,
    RPL_INFO_END,

    RPL_REGISTER_ALREADY,
    RPL_REGISTER_ALREADY_ACTIVATE,
    RPL_REGISTER_TOOMANY_EMAIL,
    RPL_REGISTER_TOOMANY_LINKS,
    RPL_REGISTER_SENDING,
    RPL_REGISTER_NOPARENT,
    RPL_REGISTER_LINKED,

    RPL_SET_UNKNOWN,
    RPL_SET_INVALID,
    RPL_SET_QUERY,
    RPL_SET,

    RPL_LASTREPLY /* always keep this at the end */
};
static int replies[RPL_LASTREPLY];

void nick_setup(void) {

    replies[RPL_LASTREPLY] = -1;
    replies[RPL_SYNTAX] = create_message("ns-syntax", "usage: %s");
    replies[RPL_HELP] = create_message("ns-help",
            "for help: /nickserv help %s");
    replies[RPL_NOTREG] = create_message("ns-notreg",
            "%s is not a registered nickname.");
    replies[RPL_NOACCESS] = create_message("ns-noaccess",
            "you do not have access to the nickname %s.");
    replies[RPL_NOACCESS_ID] = create_message("ns-noaccess-id",
            "you must identify to %s to do that.");
    replies[RPL_UNKNOWN_COMMAND] = create_message("ns-unknown-command",
            "unknown command %s.");

    replies[RPL_NOTYOURNICK] = create_message("ns-notyournick",
            "the nickname %s is registered but does not appear to belong "
            "to you.  if this is your nickname please identify to it.  if "
            "this is not your nickname please choose another.");
    replies[RPL_NICKISPROTECTED] = create_message("ns-nickisprotected",
            "this nickname has been protected by the owner from unauthorised "
            "use.  if you do not choose another nickname in %d seconds your "
            "nickname will be changed automatically.");

    replies[RPL_ACCESS_ADD_NOBANG] = create_message("ns-access-add-nobang",
            "access list entries use the form \"user@host\", not "
            "\"nick!user@host\".");
    replies[RPL_ACCESS_ADD_BADMASK] = create_message("ns-access-add-badmask",
            "the mask %s is not a valid access list mask.");
    replies[RPL_ACCESS_ADD_TOOMANY] = create_message("ns-access-add-toomany",
            "you have too many access list entries to add another.");
    replies[RPL_ACCESS_ADD_ALREADY] = create_message("ns-access-add-already",
            "%s is already in your access list.");
    replies[RPL_ACCESS_ADD] = create_message("ns-access-add",
            "the mask %s has been added to your access list.");
    replies[RPL_ACCESS_CHECK_OFFLINE] = create_message(
            "ns-access-check-offline", "%s is not online.");
    replies[RPL_ACCESS_CHECK_NONE] = create_message("ns-access-check-none",
            "%s has no access to their nickname.");
    replies[RPL_ACCESS_CHECK_LIST] = create_message("ns-access-check-list",
            "%s has access to their nickname via an access list entry.");
    replies[RPL_ACCESS_CHECK_IDENT] = create_message("ns-access-check-ident",
            "%s has access to their nickname via password authentication.");
    replies[RPL_ACCESS_DEL_NOTFOUND] = create_message("ns-access-del-notfound",
            "the mask %s is not in your access list.");
    replies[RPL_ACCESS_DEL] = create_message("ns-access-del",
            "the mask %s has been removed from your access list.");
    replies[RPL_ACCESS_LIST_START] = create_message("ns-access-list-start",
            "access list entries for %s:");
    replies[RPL_ACCESS_LIST_END] = create_message("ns-access-list-end",
            "end of access list.");
    replies[RPL_ACCESS_WIPE] = create_message("ns-access-wipe",
            "access list for %s has been wiped.");

    replies[RPL_AUTH_ALREADY] = create_message("ns-auth-already",
            "%s is already authenticated.");
    replies[RPL_AUTH_BADKEY] = create_message("ns-auth-badkey",
            "%s is not the correct key to authenticate %s");
    replies[RPL_AUTH_FINISHED] = create_message("ns-auth-finished",
            "the nickname %s is now completely registered and activated.  "
            "welcome to %s!");
    
    replies[RPL_DROP_OK] = create_message("ns-drop-ok",
            "the nickname %s has been dropped.");

    replies[RPL_IDENTIFY_BADPASS] = create_message("ns-identify-badpass",
            "incorrect password for %s.");
    replies[RPL_IDENTIFY_OK] = create_message("ns-identify-ok",
            "password accepted.  you are now identified for %s.");

    replies[RPL_INFO_START] = create_message("ns-info-start", "info for %s:");
    replies[RPL_INFO_ONLINE] = create_message("ns-info-online",
            "now online:    for extra info /whois %s");
    replies[RPL_INFO_NOTACTIVATED] = create_message("ns-info-notactivated",
            "not activated: this nickname has not yet been activated");
    replies[RPL_INFO_LASTADDR] = create_message("ns-info-lastaddr",
            "last address:  %s[%s@%s] (%s)");
    replies[RPL_INFO_PARENT] = create_message("ns-info-parent",
            "linked to:     %s");
    replies[RPL_INFO_LASTTIME] = create_message("ns-info-lasttime",
            "last seen on:  %s");
    replies[RPL_INFO_REGTIME] = create_message("ns-info-regtime",
            "registered on: %s");
    replies[RPL_INFO_TIMENOW] = create_message("ns-info-timenow",
            "current time:  %s");
    replies[RPL_INFO_ADMIN] = create_message("ns-info-admin",
            "administrator: %s is a services administrator");
    replies[RPL_INFO_EMAIL] = create_message("ns-info-email",
            "email address: %s");
    replies[RPL_INFO_PROTECT] = create_message("ns-info-protect",
            "protected    : this nickname cannot be used by others.");
    replies[RPL_INFO_END] = create_message("ns-info-end",
            "end of info for %s");

    replies[RPL_REGISTER_ALREADY] = create_message("ns-register-already",
            "this nickname has already been registered.");
    replies[RPL_REGISTER_ALREADY_ACTIVATE] = create_message(
            "ns-register-already-activate",
            "to activate this nickname use: /nickserv activate %s "
            "<activation-key>");
    replies[RPL_REGISTER_TOOMANY_EMAIL] = create_message(
            "ns-register-toomany-email",
            "the email address %s is in use by too many nicknames.");
    replies[RPL_REGISTER_TOOMANY_LINKS] = create_message(
            "ns-register-toomany-links",
            "too many nicknames have been linked to %s.");
    replies[RPL_REGISTER_SENDING] = create_message("ns-register-sending",
            "thank you for your registration request!  a confirmation "
            "email has been sent to %s and should arrive soon.");
    replies[RPL_REGISTER_NOPARENT] = create_message("ns-register-noparent",
            "you must identify to the nickname you wish to register this "
            "nickname under first.");
    replies[RPL_REGISTER_LINKED] = create_message("ns-register-linked",
            "the nickname %s has been registered and is linked to %s.");

    replies[RPL_SET_UNKNOWN] = create_message("ns-set-unknown",
            "unknown setting %s.");
    replies[RPL_SET_INVALID] = create_message("ns-set-invalid",
            "invalid setting \"%s\" for %s.");
    replies[RPL_SET_QUERY] = create_message("ns-set-query",
            "%s setting on %s is \"%s\".");
    replies[RPL_SET] = create_message("ns-set",
            "set %s on %s to \"%s\".");
}

#define send_syntax(cli, cmd, args) do {                                \
    send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_SYNTAX]),        \
            cmd " " args);                                                \
    send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_HELP]),        \
            cmd);                                                        \
} while (0)

void nick_handle_msg(client_t *cli, int argc, char **argv) {

    /* Handle the ping request specially here */
    if (!strcasecmp(argv[0], "\001PING")) {
        if (argc == 2)
            send_reply(cli, &services.nick, "%s %s", argv[0], argv[1]);
        else if (argc == 3)
            send_reply(cli, &services.nick, "%s %s %s", argv[0], argv[1],
                    argv[2]);
    } else if (!strcasecmp(argv[0], "ACCESS"))
        ns_access(cli, argc, argv);
    else if (!strcasecmp(argv[0], "AUTH"))
        ns_auth(cli, argc, argv);
    else if (!strcasecmp(argv[0], "DROP"))
        ns_drop(cli, argc, argv);
    else if (!strncasecmp(argv[0], "IDENT", 5))
        ns_identify(cli, argc, argv);
    else if (!strcasecmp(argv[0], "INFO"))
        ns_info(cli, argc, argv);
    else if (!strcasecmp(argv[0], "REGISTER"))
        ns_register(cli, argc, argv);
    else if (!strcasecmp(argv[0], "SET"))
        ns_set(cli, argc, argv);
    else {
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_UNKNOWN_COMMAND]), argv[0]);
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_HELP]), "");
    }
}

/* This hook actually serves for both client connects and disconnects.  On
 * connect we check to see if they're using a registered nickname, and on
 * disconnect we see if they've identified for a nickname and update the last
 * seen time and stuff. */
HOOK_FUNCTION(nick_register_hook) {
    client_t *cli = (client_t *)data;
    struct services_client_data *scdp =
        (struct services_client_data *)mdext(cli, services.mdext.client);
    regnick_t *np;
    
    if (MYCLIENT(cli))
        return NULL; /* local clients are ignored. */

    if (ep == ircd.events.register_client) {
        scdp->nick = NULL;
        if ((np = db_find_nick(cli->nick)) != NULL &&
                has_access_to_nick(cli, np, NICK_IFL_ACCESS)) {
            scdp->nick = np;
            np->flags |= NICK_IFL_MASKACCESS;
        } else
            return NULL; /* nick is not registered.. */

        if (!regnick_access(np))
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_NOTYOURNICK]), cli->nick);
    } else {
        if (scdp->nick != NULL) {
            scdp->nick->last = me.now;
            scdp->nick->flags &= ~(NICK_IFL_MASKACCESS | NICK_IFL_IDACCESS);
        }
    }

    return NULL;
}

static SERVICES_COMMAND(ns_access) {
    regnick_t *np;
    struct services_client_data *scdp =
        (struct services_client_data *)mdext(cli, services.mdext.client);
    struct regnick_access *rap;

    if (argc < 2) {
        send_syntax(cli, "ACCESS", "<ADD|CHECK|DEL|LIST|WIPE> [mask | nick]");
        return;
    }

    if (!strcasecmp(argv[1], "CHECK")) {
        client_t *cp;
        if (argc > 2)
            cp = find_client(argv[2]);
        else
            cp = cli;

        if (cp == NULL) {
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_ACCESS_CHECK_OFFLINE]), argv[2]);
            return;
        }
        scdp = (struct services_client_data *)mdext(cp, services.mdext.client);
        if ((np = db_find_nick(cp->nick)) == NULL)
            send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_NOTREG]),
                    cp->nick);
        else if (scdp->nick != np || !regnick_access(np))
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_ACCESS_CHECK_NONE]), cp->nick);
        else if (regnick_access(np) && !regnick_idaccess(np))
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_ACCESS_CHECK_LIST]), cp->nick);
        else
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_ACCESS_CHECK_IDENT]), cp->nick);

        return;
    }

    /* The last four subcommands all require identification access */
    if ((np = db_find_nick(cli->nick)) == NULL) {
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_NOTREG]),
                cli->nick);
        return;
    } else if (!has_access_to_nick(cli, np, NICK_IFL_IDACCESS)) {
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_NOACCESS_ID]),
                np->name);
        return;
    }

    if (!strcasecmp(argv[1], "ADD")) {
        char *s;
        int len, ac_count;

        if (argc < 3) {
            send_syntax(cli, "ACCESS", "ADD <mask>");
            return;
        }

        /* now examine the mask.  check for common mistakes and abuses here
         * (basically look for the ! symbol and make sure the host portion is
         * at least somewhat alphanumeric.) */
        if ((s = strchr(argv[2], '!')) != NULL) {
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_ACCESS_ADD_NOBANG]));
            return;
        }
        if ((s = strchr(argv[2], '@')) == NULL)
            s = argv[2];
        len = ac_count = 0;
        while (*s != '\0') {
            if (isalnum(*s++))
                ac_count++;
            len++;
        }
        if (ac_count < len / 2) {
            /* if alphanumeric characters are not at least 50% of the mask,
             * this is probably an obnoxious abusive mask. */
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_ACCESS_ADD_BADMASK]), argv[2]);
            return;
        }

        /* have determined that it is a suitable mask, count the number of
         * access list entries and make sure it's not already in there.  if the
         * user passes these two counts then we can add the entry. */
        ac_count = 0;
        SLIST_FOREACH(rap, &np->alist, lp) {
            ac_count++;
            if (ac_count >= services.limits.nick_acclist) {
                send_reply(cli, &services.nick, MSG_FMT(cli,
                            replies[RPL_ACCESS_ADD_TOOMANY]));
                return;
            }
        }
        if (regnick_access_find(np, argv[2]) != NULL) {
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_ACCESS_ADD_ALREADY]), argv[2]);
            return;
        }
        regnick_access_add(np, argv[2]);
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_ACCESS_ADD]),
                argv[2]);
        return;
    }

    if (!strcasecmp(argv[1], "DEL")) {

        if (argc < 3) {
            send_syntax(cli, "ACCESS", "DEL <mask>");
            return;
        }
        
        if ((rap = regnick_access_find(np, argv[2])) == NULL) {
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_ACCESS_DEL_NOTFOUND]), argv[2]);
            return;
        }

        regnick_access_del(np, rap);
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_ACCESS_DEL]),
                argv[2]);
        return;
    }

    if (!strcasecmp(argv[1], "LIST")) {
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_ACCESS_LIST_START]), np->name);
        SLIST_FOREACH(rap, &np->alist, lp)
            send_reply(cli, &services.nick, "%s@%s", rap->user, rap->host);
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_ACCESS_LIST_END]));
        return;
    }

    if (!strcasecmp(argv[1], "WIPE")) {
        while (!SLIST_EMPTY(&np->alist))
            regnick_access_del(np, SLIST_FIRST(&np->alist));
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_ACCESS_WIPE]), np->name);
        return;
    }

    send_syntax(cli, "ACCESS", "<ADD|CHECK|DEL|LIST|WIPE> [mask | nick]");
}

static SERVICES_COMMAND(ns_auth) {
    regnick_t *np;
    uint64_t key;

    if (argc < 3) {
        send_syntax(cli, "AUTH", "<nickname> <authentication-key>");
        return;
    }

    if ((np = db_find_nick(argv[1])) == NULL) {
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_NOTREG]),
                argv[1]);
        return;
    } else if (regnick_activated(np)) {
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_AUTH_ALREADY]), np->name);
        return;
    }

    /* now check the key.. */
    key = strtoll(argv[2], NULL, 10);
    if (key != np->flags) {
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_AUTH_BADKEY]),
                argv[2]);
        return;
    }

    /* everything is kosher!  excellent. */
    np->flags = 0;
    np->intflags |= NICK_IFL_ACTIVATED;
    np->intflags &= ~NICK_IFL_MAILED;
    
    send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_AUTH_FINISHED]),
            np->name, ircd.network_full);
}

static SERVICES_COMMAND(ns_drop) {
    regnick_t *np;
    struct services_client_data *scdp =
        (struct services_client_data *)mdext(cli, services.mdext.client);

    if (argc < 2) {
        send_syntax(cli, "DROP", "<nickname>");
        return;
    }

    if ((np = db_find_nick(argv[1])) == NULL) {
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_NOTREG]),
                argv[1]);
        return;
    }

    if (!has_access_to_nick(cli, np, NICK_IFL_IDACCESS)) {
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_NOACCESS_ID]), argv[1]);
        return;
    }

    send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_DROP_OK]),
            np->name);
    scdp->nick = NULL;
    regnick_destroy(np);
}

static SERVICES_COMMAND(ns_identify) {
    regnick_t *np;
    struct services_client_data *scdp =
        (struct services_client_data *)mdext(cli, services.mdext.client);
    char *nick, *pass;

    if (argc < 2) {
        send_syntax(cli, "IDENTIFY", "[nickname] <password>");
        return;
    }

    if (argc < 3) {
        nick = cli->nick;
        pass = argv[1];
    } else {
        nick = argv[1];
        pass = argv[2];
    }
    if ((np = db_find_nick(nick)) == NULL) {
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_NOTREG]),
                nick);
        return;
    }

    if (strcmp(np->pass, pass)) {
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_IDENTIFY_BADPASS]), nick);
        return;
    }

    /* If we haven't returned yet they identified successfully.  Slick. */
    send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_IDENTIFY_OK]),
            np->name);
    if (scdp->nick != NULL)
        scdp->nick->intflags &= ~NICK_IFL_ACCESS;
    scdp->nick = np;
    np->intflags |= NICK_IFL_IDACCESS;
}

static SERVICES_COMMAND(ns_info) {
    client_t *cp;
    regnick_t *np;
    struct tm *tp;
    char timebuf[128];

    if ((np = db_find_nick((argc < 2 ? cli->nick : argv[1]))) == NULL) {
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_NOTREG]),
                (argc < 2 ? cli->nick : argv[1]));
        return;
    }

    /* now start sending the info.. */
    send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_INFO_START]),
            np->name);
    if ((cp = find_client(np->name)) != NULL)
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_INFO_ONLINE]),
                cp->nick);
    if (regnick_activated(np)) {
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_INFO_LASTADDR]), np->name, np->user, np->host,
                np->info);
        if (np->parent != NULL)
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_INFO_PARENT]), np->parent->name);
    } else
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_INFO_NOTACTIVATED]));
    tp = gmtime(&np->last);
    strftime(timebuf, 128, "%a %d/%m/%Y %T %Z", tp);
    send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_INFO_LASTTIME]),
            timebuf);
    tp = gmtime(&np->regtime);
    strftime(timebuf, 128, "%a %d/%m/%Y %T %Z", tp);
    send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_INFO_REGTIME]),
            timebuf);
    tp = gmtime(&me.now);
    strftime(timebuf, 128, "%a %d/%m/%Y %T %Z", tp);
    send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_INFO_TIMENOW]),
            timebuf);
    if (np->intflags & NICK_IFL_ADMIN)
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_INFO_ADMIN]),
                np->name);
    if (np->flags & NICK_FL_SHOWEMAIL || OPER(cli))
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_INFO_EMAIL]),
                np->email);
    if (np->flags & NICK_FL_PROTECT)
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_INFO_PROTECT]));
    send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_INFO_END]),
            np->name);
}

/* The register command may be called either with an email address (to register
 * a 'primary') account, or with a nickname to register a 'secondary' account
 * tied to that nickname.  The second form requires identification-level access
 * to the nickname in question. */
static SERVICES_COMMAND(ns_register) {
    struct services_client_data *scdp =
        (struct services_client_data *)mdext(cli, services.mdext.client);
    regnick_t *np;
    char *s;
    int i;

    if (argc < 3) {
        send_syntax(cli, "REGISTER", "<password> <email | nickname>");
        return;
    }
    if ((np = db_find_nick(cli->nick)) != NULL) {
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_REGISTER_ALREADY]));
        if (!regnick_activated(np))
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_REGISTER_ALREADY_ACTIVATE]), np->name);
        return;
    }

    /* Okay, the nickname isn't already registered.  See if the second argument
     * was an email address and continue on if it was. */
    if ((s = strchr(argv[2], '@')) != NULL) {
        if ((s = strchr(s, '.')) != NULL) {
            /* looks good.. */
            struct mail_contact *mcp = db_find_mail(argv[2]);

            if (mcp != NULL && mcp->nicks >= services.limits.mail_nicks) {
                send_reply(cli, &services.nick, MSG_FMT(cli,
                            replies[RPL_REGISTER_TOOMANY_EMAIL]),
                        mcp->address);
                return;
            }

            np = regnick_create(cli->nick);
            strcpy(np->user, cli->user);
            strcpy(np->host, cli->host);
            strcpy(np->pass, argv[1]);
            strcpy(np->info, cli->info);
            np->email = mcp;
            np->email->nicks++;
            np->last = me.now;
            np->regtime = me.now;
            /* generate the activation key..  this works differently depending
             * on whether word size is four or eight bytes */
            srand((unsigned)cli + (unsigned)np);
#if SIZEOF_INT == 8
            np->flags = rand();
#else
            np->flags = rand();
            np->flags = ((uint64_t)rand()) << 32;
#endif

            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_REGISTER_SENDING]), np->email);
            return;
        }
    } 
    
    /* If we didn't return, then maybe they're doing the second (nick linking)
     * form of registration.  Hrm.  See if they're currently identified for the
     * nick they provided, and if they are, go ahead and do the link. */
    if ((np = db_find_nick(argv[2])) == NULL) {
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_NOTREG]),
                argv[1]);
        return;
    } else if (!has_access_to_nick(cli, np, NICK_IFL_IDACCESS)) {
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_REGISTER_NOPARENT]));
        return;
    }

    /* Count the number of nicknames the parent currently has.  If it's over
     * the limit, then we deny the registration. */
    i = 0;
    LIST_FOREACH(np, &scdp->nick->links, linklp)
        i++;
    if (i >= services.limits.linked_nicks) {
        send_reply(cli, &services.nick, MSG_FMT(cli,
                    replies[RPL_REGISTER_TOOMANY_LINKS]), scdp->nick->name);
        return;
    }

    /* Okay, they're identified to their desired parent.  Do the whole
     * creation/setup/etc thing. */
    np = regnick_create(cli->nick);
    strcpy(np->user, cli->user);
    strcpy(np->host, cli->host);
    strcpy(np->pass, argv[1]);
    strcpy(np->info, cli->info);
    np->email = scdp->nick->email;
    np->email->nicks++;
    np->last = me.now;
    np->regtime = me.now;
    np->intflags |= NICK_IFL_ACTIVATED;

    /* Don't forget to add them to the parent.. */
    np->parent = scdp->nick;
    LIST_INSERT_HEAD(&scdp->nick->links, np, linklp);

    send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_REGISTER_LINKED]),
            np->name, scdp->nick->name);
}

static SERVICES_COMMAND(ns_set) {
    struct services_client_data *scdp =
        (struct services_client_data *)mdext(cli, services.mdext.client);
    regnick_t *np;
    int oarg;

    if (argc < 2) {
        send_syntax(cli, "SET", "[on <nickname>] <setting> [value]");
        return;
    }
    if (!strcasecmp(argv[1], "on")) {
        if (argc < 4) {
            send_syntax(cli, "SET", "ON <nickname> <setting> [value]");
            return;
        }
        if ((np = db_find_nick(argv[2])) == NULL) {
            send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_NOTREG]),
                argv[2]);
            return;
        }
        oarg = 3;
    } else {
        if (scdp->nick == NULL) {
            send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_NOTREG]),
                cli->nick);
            return;
        }
        np = scdp->nick;
        oarg = 1;
    }

    if (!has_access_to_nick(cli, np, NICK_IFL_IDACCESS)) {
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_NOACCESS_ID]),
                np->name);
        return;
    }

    if (!strcasecmp(argv[oarg], "EMAIL")) {
        if (++oarg >= argc) {
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_SET_QUERY]), "email address",
                    np->name,
                    (np->flags & NICK_FL_SHOWEMAIL ? "public" : "private"));
            return;
        }

        if (!strcasecmp(argv[oarg], "PUBLIC"))
            np->flags |= NICK_FL_SHOWEMAIL;
        else if (!strcasecmp(argv[oarg], "PRIVATE"))
            np->flags &= ~NICK_FL_SHOWEMAIL;
        else {
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_SET_INVALID]), argv[oarg], "EMAIL");
            return;
        }
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_SET]),
                "email address", np->name,
                (np->flags & NICK_FL_SHOWEMAIL ? "public" : "private"));
    } else if (!strcasecmp(argv[oarg], "PROTECT")) {
        if (++oarg >= argc) {
            send_reply(cli, &services.nick, MSG_FMT(cli,
                        replies[RPL_SET_QUERY]), "protection",
                    np->name,
                    (np->flags & NICK_FL_PROTECT ? "on" : "off"));
            return;
        }

        switch (str_conv_bool(argv[oarg], -1)) {
            case 0:
                np->flags &= ~NICK_FL_PROTECT;
                break;
            case 1:
                np->flags |= NICK_FL_PROTECT;
                break;
            default:
                send_reply(cli, &services.nick, MSG_FMT(cli,
                            replies[RPL_SET_INVALID]), argv[oarg], "PROTECT");
                return;
        }
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_SET]),
                "protection", np->name,
                (np->flags & NICK_FL_PROTECT ? "on" : "off"));
    } else
        send_reply(cli, &services.nick, MSG_FMT(cli, replies[RPL_SET_UNKNOWN]),
                argv[oarg]);

}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
