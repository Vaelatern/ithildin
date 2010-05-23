/*
 * watch.c: the WATCH command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 *
 * This is a fairly complicated system.  Probably one of the most complicated
 * of the commands.  Basically, it provides server-side notify.  This doesn't
 * really work in a 'lazy leaves' style system (which may or may not exist at
 * some point), but in a regular style network it provides a good way to ease
 * the pain of repeated ISONs from users with large notify lists.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: watch.c 771 2006-09-19 17:49:30Z wd $");

MODULE_REGISTER("$Rev: 771 $");
/*
@DEPENDENCIES@: ircd
*/

typedef struct watch watch_t;
LIST_HEAD(watchusers, watchlink);
LIST_HEAD(userwatches, watchlink);

struct watchlink {
    client_t *cli;
    watch_t *watch;

    LIST_ENTRY(watchlink) lpcli;
    LIST_ENTRY(watchlink) lpwtch;
};

/* an individual watch entry.  contains the last time the nick signed on (dunno
 * why), the nickname, and a list of users who are watching this nick. */
struct watch {
    char    nick[NICKLEN + 1];        /* the nick */
    time_t  last;                /* last signon/signoff for this nick */
    int            count;                /* number of people watching this nick */

    struct watchusers users;
};

/* each client has a 'client_watches' member which contains a list of the
 * client's watches, and the count.  we bastardize the userchans/chanusers
 * relationship here. */
struct client_watches {
    struct userwatches list;        /* all our watches */
    int            count;                /* and the count */
};

static struct {
    int watchlim;        /* privilege for watch size limit */
    struct mdext_item *mdext;
    hashtable_t *table;        /* table of watches */
    /* it might be interesting to keep some statistics here.  we don't, yet */
} watch;

/* here are the functions to support watch-stuffs. */
static watch_t *create_watch(char *);
static void destroy_watch(watch_t *);
static struct watchlink *find_watch_link(client_t *, watch_t *);
static void add_to_watch(watch_t *, client_t *);
static void del_from_watch(watch_t *, client_t *);
HOOK_FUNCTION(watch_client_hook);
#define find_watch(x) (struct watch *)hash_find(watch.table, x)

MODULE_LOADER(watch) {
    int64_t i64 = 128; /* default watch limit */

    if (!get_module_savedata(savelist, "watch", &watch)) {
        watch.watchlim = create_privilege("maxwatch", PRIVILEGE_FL_INT,
                &i64, NULL);
        watch.mdext = create_mdext_item(ircd.mdext.client,
                sizeof(struct client_watches));
        watch.table = create_hash_table(hashtable_size(ircd.hashes.client),
                offsetof(watch_t, nick), NICKLEN,
                HASH_FL_NOCASE|HASH_FL_STRING, "nickcmp");
    }

    add_isupport("WATCH", ISUPPORT_FL_PRIV, (char *)&watch.watchlim);

    add_hook(ircd.events.register_client, watch_client_hook);
    add_hook(ircd.events.unregister_client, watch_client_hook);
    add_hook(ircd.events.client_nick, watch_client_hook);

    /* now create numerics.  the toomanywatch numeric is a bogon, according to
     * various rfcs, because 5xx numbers are supposed to be permanent errors
     * (and that is not, technically, a permanent error), not that IRC numerics
     * have anything to do with anybody else's, but still. */
#define ERR_TOOMANYWATCH 512
    CMSG("512", "%s :Maximum size for WATCH-list is %d entries");
#define RPL_LOGON 600
    CMSG("600", "%s %s %s %d :logged online");
#define RPL_LOGOFF 601
    CMSG("601", "%s %s %s %d :logged offline");
#define RPL_WATCHOFF 602
    CMSG("602", "%s %s %s %d :stopped watching");
#define RPL_WATCHSTAT 603
    CMSG("603", ":You have %d WATCH entries and are on %d (local) entries");
#define RPL_NOWON 604
    CMSG("604", "%s %s %s %d :is online");
#define RPL_NOWOFF 605
    CMSG("605", "%s %s %s %d :is offline");
#define RPL_WATCHLIST 606
    CMSG("606", ":%s");
#define RPL_ENDOFWATCHLIST 607
    CMSG("607", ":End of /WATCH %c");

    return 1;
}
MODULE_UNLOADER(watch) {

    if (reload)
        add_module_savedata(savelist, "watch", sizeof(watch), &watch);
    else {
        destroy_privilege(watch.watchlim);
        destroy_mdext_item(ircd.mdext.client, watch.mdext);
        destroy_hash_table(watch.table);
    }

    del_isupport(find_isupport("WATCH"));

    remove_hook(ircd.events.register_client, watch_client_hook);
    remove_hook(ircd.events.unregister_client, watch_client_hook);
    remove_hook(ircd.events.client_nick, watch_client_hook);

    DMSG(ERR_TOOMANYWATCH);
    DMSG(RPL_LOGON);
    DMSG(RPL_LOGOFF);
    DMSG(RPL_WATCHOFF);
    DMSG(RPL_WATCHSTAT);
    DMSG(RPL_NOWON);
    DMSG(RPL_NOWOFF);
    DMSG(RPL_WATCHLIST);
    DMSG(RPL_ENDOFWATCHLIST);
}

CLIENT_COMMAND(watch, 0, 1, COMMAND_FL_FOLDMAX) {
    struct client_watches *cwp =
        (struct client_watches *)mdext(cli, watch.mdext);
    watch_t *wp;
    struct watchlink *wlp;
    client_t *cp;
    char *buf = (argc > 1 ? argv[1] : "l");
    char *cur;

    while ((cur = strsep(&buf, ", \t")) != NULL) {
        if (*cur == '\0')
            continue;
        if (*cur == '+') {
            /* addition case */
            cur++;
            if (*cur == '\0')
                continue;
            /* check their count */
            if (cwp->count >= IPRIV(cli, watch.watchlim)) {
                sendto_one(cli, RPL_FMT(cli, ERR_TOOMANYWATCH), cur,
                        IPRIV(cli, watch.watchlim));
                continue;
            }
            /* only allow real, valid nicknames */
            if (check_nickname(cur) && strlen(cur) <= NICKLEN) {
                if ((wp = find_watch(cur)) == NULL)
                    wp = create_watch(cur);
                if (find_watch_link(cli, wp) == NULL)
                    add_to_watch(wp, cli);
                if ((cp = find_client(cur)) != NULL)
                    sendto_one(cli, RPL_FMT(cli, RPL_NOWON), cp->nick,
                            cp->user, cp->host, cp->signon);
                else
                    sendto_one(cli, RPL_FMT(cli, RPL_NOWOFF),
                            cur, "*", "*", 0);
            }
        } else if (*cur == '-') {
            /* removal case */
            cur++;
            if (*cur == '\0')
                continue;
            /* find the watch and remove them from it if we do */
            if ((wp = find_watch(cur)) != NULL)
                del_from_watch(wp, cli);
            if ((cp = find_client(cur)) != NULL)
                    sendto_one(cli, RPL_FMT(cli, RPL_WATCHOFF), cp->nick,
                            cp->user, cp->host, cp->signon);
                else
                    sendto_one(cli, RPL_FMT(cli, RPL_WATCHOFF),
                            cur, "*", "*", 0);
        } else if (tolower(*cur) == 'c') {
            while (cwp->count > 0) {
                wlp = LIST_FIRST(&cwp->list);
                del_from_watch(wlp->watch, cli);
            }
        } else if (tolower(*cur) == 's') {
            /* send statistics and a list of the client's watches. */
            char sbuf[320];
            int sblen = 0;

            /* XXX: static sized buffer-fuck.  this must be cleaned up */
            wp = find_watch(cli->nick);
            sendto_one(cli, RPL_FMT(cli, RPL_WATCHSTAT), cwp->count,
                    (wp != NULL ? wp->count : 0));
            LIST_FOREACH(wlp, &cwp->list, lpwtch) {
                if (310 - sblen <= strlen(wlp->watch->nick)) {
                    /* send the list if it's getting full */
                    sendto_one(cli, RPL_FMT(cli, RPL_WATCHLIST), sbuf);
                    sblen = 0;
                }
                sblen += snprintf(sbuf + sblen, 310 - sblen, "%s ",
                        wlp->watch->nick);
            }
            if (sblen != 0)
                sendto_one(cli, RPL_FMT(cli, RPL_WATCHLIST), sbuf);
            sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWATCHLIST), *cur);
        } else if (tolower(*cur) == 'l') {
            /* the list case.. */
            LIST_FOREACH(wlp, &cwp->list, lpwtch) {
                if ((cp = find_client(wlp->watch->nick)) != NULL)
                    sendto_one(cli, RPL_FMT(cli, RPL_NOWON), cp->nick,
                            cp->user, cp->host, cp->signon);
                else if (*cur == 'L')
                    /* if they asked for a full list (L), send offline users
                       too */
                    sendto_one(cli, RPL_FMT(cli, RPL_NOWOFF), wlp->watch->nick,
                            "*", "*", wlp->watch->last);
            }
            sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWATCHLIST), *cur);
        }
    }
    return COMMAND_WEIGHT_MEDIUM;
}

static watch_t *create_watch(char *nick) {
    watch_t *wp = malloc(sizeof(watch_t));

    strlcpy(wp->nick, nick, NICKLEN + 1);
    wp->last = 0;
    wp->count = 0;
    hash_insert(watch.table, wp);
    LIST_INIT(&wp->users);

    return wp;
}

static void destroy_watch(watch_t *wp) {

    hash_delete(watch.table, wp);
    free(wp);
}

static struct watchlink *find_watch_link(client_t *cli, watch_t *wp) {
    struct client_watches *cwp =
        (struct client_watches *)mdext(cli, watch.mdext);
    struct watchlink *wlp;

    LIST_FOREACH(wlp, &cwp->list, lpwtch) {
        if (wlp->watch == wp)
            return wlp;
    }

    return NULL;
}

static void add_to_watch(watch_t *wp, client_t *cli) {
    struct client_watches *cwp =
        (struct client_watches *)mdext(cli, watch.mdext);
    struct watchlink *wlp = malloc(sizeof(struct watchlink));

    wlp->cli = cli;
    wlp->watch = wp;
    cwp->count++;
    wp->count++;
    LIST_INSERT_HEAD(&cwp->list, wlp, lpwtch);
    LIST_INSERT_HEAD(&wp->users, wlp, lpcli);
}

static void del_from_watch(watch_t *wp, client_t *cli) {
    struct client_watches *cwp =
        (struct client_watches *)mdext(cli, watch.mdext);
    struct watchlink *wlp = find_watch_link(cli, wp);

    if (wlp != NULL) {
        LIST_REMOVE(wlp, lpwtch);
        LIST_REMOVE(wlp, lpcli);
        free(wlp);
        cwp->count--;
        wp->count--;
    }

    if (wp->count == 0)
        destroy_watch(wp);
}

HOOK_FUNCTION(watch_client_hook) {
    client_t *cli = (client_t *)data;
    watch_t *wp;
    struct watchlink *wlp;

    if (ep == ircd.events.register_client) {
        /* okay, notify all watchers that this user has signed on. */
        if ((wp = find_watch(cli->nick)) != NULL) {
            wp->last = cli->signon;
            LIST_FOREACH(wlp, &wp->users, lpcli) {
                sendto_one(wlp->cli, RPL_FMT(wlp->cli, RPL_LOGON), cli->nick,
                        cli->user, cli->host, cli->signon);
            }
        }
    } else if (ep == ircd.events.unregister_client) {
        /* client is signing off.  do the watch signoff thing.  if it's one of
         * ours, clear out their watch list. */
        if ((wp = find_watch(cli->nick)) != NULL) {
            wp->last = me.now;
            LIST_FOREACH(wlp, &wp->users, lpcli) {
                sendto_one(wlp->cli, RPL_FMT(wlp->cli, RPL_LOGOFF), cli->nick,
                        cli->user, cli->host, me.now);
            }
        }

        if (MYCLIENT(cli)) {
            struct client_watches *cwp =
                (struct client_watches *)mdext(cli, watch.mdext);

            while (cwp->count > 0) {
                wlp = LIST_FIRST(&cwp->list);
                del_from_watch(wlp->watch, cli);
            }
        }
    } else if (ep == ircd.events.client_nick) {
        /* this is a nickname change.  we have to handle both signoff AND
         * signon cases in this event. */
        cli = (client_t *)data;

        /* first do the signoff.. */
        if ((wp = find_watch(cli->hist->nick)) != NULL) {
            wp->last = me.now;
            LIST_FOREACH(wlp, &wp->users, lpcli) {
                sendto_one(wlp->cli, RPL_FMT(wlp->cli, RPL_LOGOFF),
                        cli->hist->nick, cli->user, cli->host, me.now);
            }
        }
        /* now the signon */
        if ((wp = find_watch(cli->nick)) != NULL) {
            wp->last = cli->ts;
            LIST_FOREACH(wlp, &wp->users, lpcli) {
                sendto_one(wlp->cli, RPL_FMT(wlp->cli, RPL_LOGON), cli->nick,
                        cli->user, cli->host, cli->signon);
            }
        }
    }
    return NULL;
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
