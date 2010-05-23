/*
 * client.c: client structure management functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file provides mechanisms for creating/destroy client structures, as
 * well as registering them on the server and setting/unsetting user modes on
 * them.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: client.c 831 2009-02-09 00:42:56Z wd $");

/* this creates a new client structure and places it in the 'conn' structure
 * passed, if any.  If there is no conn structure, then this is either a remote
 * client or a pseudo-client. */
client_t *create_client(connection_t *conn) {
    client_t *cp = calloc(1, sizeof(client_t));
    cp->conn = conn;

    /* set connection time/etc */
    cp->ts = cp->last = me.now;
    cp->orighost = cp->host; /* don't forget this one */

    cp->mdext = mdext_alloc(ircd.mdext.client);

    /* all new clients are set unknown even if they're remote (this will get
     * handled in register_client so it shouldn't be a big deal */
    cp->flags |= IRCD_CLIENT_UNKNOWN;
    ircd.stats.serv.unkclients++;

    if (conn != NULL) {
        conn->cli = cp;
        cp->pset = conn->cls->pset;
        get_socket_address(isock_raddr(conn->sock), cp->ip, IPADDR_MAXLEN + 1,
                NULL);
    } else
        cp->pset = LIST_FIRST(ircd.privileges.sets);

    LIST_INSERT_HEAD(ircd.lists.clients, cp, lp);
    return cp;
}

/* this destroys a client, and if it is our connection, also destroys the
 * client's connection.  do *NOT* try to destroy connections after
 * destroying their clients. */
void destroy_client(client_t *cli, char *msg) {
    struct chanlink *clp;
    unsigned char *s;

    if (CLIENT_HISTORY(cli)) {
        /* just return the memory */
        mdext_free(ircd.mdext.client, cli->mdext);
        free(cli);
        return;
    }

    if (CLIENT_REGISTERED(cli)) { /* only do this stuff for registered
                                     clients */
        if (MYCLIENT(cli))
            hook_event(ircd.events.client_disconnect, cli);
        hook_event(ircd.events.unregister_client, cli);

        /* if the client hasn't been flagged as 'killed' (that is, destroyed by
         * some other action) we propogate the quit message to other servers.
         * whatever betide we send the quit to all local users! */
        if (!(cli->flags & IRCD_CLIENT_KILLED))
            sendto_serv_butone(cli->server, cli, NULL, NULL, "QUIT", ":%s",
                    msg);

        clp = LIST_FIRST(&cli->chans);
        if (clp != NULL) {
            /* if we are in channels */
            /* don't send a message to our user */
            if (MYCLIENT(cli))
                ircd.sends[cli->conn->sock->fd] = 1;
            sendto_common_channels(cli, NULL, "QUIT", ":%s", msg);
            while (clp != NULL) {
                del_from_channel(cli, clp->chan, true);
                clp = LIST_FIRST(&cli->chans);
            }
        }

        /* add them to the client history, and also sign them off. */
        client_add_history(cli);
        /* and mark the client structure as a 'history' structure */
        cli->flags |= IRCD_CLIENT_HISTORY;

        /* unset all their modes.  this allows any resource cleanup to be
         * handled by individual mode handlers. */
        s = ircd.umodes.avail;
        while (*s)
            usermode_unset(*s++, cli, cli, NULL, NULL);

        if (MYCLIENT(cli)) {
            int i;

            ircd.stats.serv.curclients--;
            for (i = 0;i < ircd.sflag.size;i++)
                remove_from_send_flag(i, cli, true);
        }
        /* they would now be visible, except we're nerfing them. */
        ircd.stats.net.visclients--;
        ircd.stats.net.curclients--;
    }

    /* Oops.  We have to delete clients from the hashtable even if they aren't
     * registered.  Thanks dave. */
    if (*cli->nick != '\0')
        hash_delete(ircd.hashes.client, cli);

    if (CLIENT_UNKNOWN(cli))
        ircd.stats.serv.unkclients--;

    if (cli->conn != NULL) { /* dump it if it's ours */
        cli->conn->cli = NULL;

        /* If sendq_flush does not close the connection for us (it will
         * return 0 if it does) then close the connection.  This ensures
         * that we do best-effort work on dumping the last dribbly bits of
         * the sendq out to the client if we can.  We do not try hard to
         * make this work, though! */
        if (sendq_flush(cli->conn))
            destroy_connection(cli->conn, msg);
    }

    LIST_REMOVE(cli, lp);

    if (!CLIENT_HISTORY(cli)) {
        /* if the client was registered then we need to preserve their client
         * entry for the history list */
        mdext_free(ircd.mdext.client, cli->mdext);
        free(cli);
    }
}

/* this function changes a client's nickname, and performs all associations
 * necessary (moving hash structures, and adding the old nickname into the
 * client history).  It does *NOT* verify that 'to' is a suitable nickname,
 * however, as that is left up to the caller. */
void client_change_nick(client_t *cli, char *to) {
    int casechng = !istrcmp(ircd.maps.nick, cli->nick, to);
    
    /* check to see if this is just a case change.  if it is, istrcmp will
     * return 0.  if it's a case change, we don't do anything except set the
     * new nickname in the client structure. */
    if (*cli->nick != '\0' && !casechng) {
        hash_delete(ircd.hashes.client, cli);

        /* History entries for unregistered clients are extremely useless,
         * and detrimental sometimes. */
        if (CLIENT_REGISTERED(cli))
            client_add_history(cli);
    }

    strncpy(cli->nick, to, NICKLEN);

    if (!casechng) {
        hash_insert(ircd.hashes.client, cli);

        /* We only hook client_nick for registered clients, there is little
         * value in hooking it for nonregistered folks. */
        if (CLIENT_REGISTERED(cli))
            hook_event(ircd.events.client_nick, cli);
    }
}

/* find a client with the given name, but only if it is registered */
client_t *find_client(char *name) {
    client_t *cp;

    if ((cp = find_client_any(name)) != NULL && CLIENT_REGISTERED(cp))
        return cp;
    return NULL;
}

/* register a client, the appropriate stuff should already be filled in */
int register_client(client_t *cli) {
    connection_t *cp = cli->conn;
    void **returns; /* hook returns */
    int x = 0;
                
    /* determine if it is okay for them to connect.  if it is, and we allow the
     * connection through, let the rest of the network know before we do
     * anything else! */
    if (MYCLIENT(cli)) {
        /* only do this stuff if it's a real client, and not a fake one */
        if (cp != NULL) {
            /* check to see if our client is stage3 okay */
            returns = hook_event(ircd.events.stage3_connect, cp);
            while (x < hook_num_returns) {
                if (returns[x] != NULL) { /* denied, with a reason */
                    destroy_client(cli, (char *)returns[x]);
                    return IRCD_CONNECTION_CLOSED; /* not successful */
                }
                x++;
            }
            /* remove our client from the stage2 list, and put it in the stage3
             * list. */
            LIST_REMOVE(cp, lp);
            LIST_INSERT_HEAD(ircd.connections.clients, cp, lp);
        }

        cli->signon = cli->ts = me.now;
        cli->hops = 0; /* our client, they're 0 hops from us <G> */

        /* only hook for local registration */
        hook_event(ircd.events.client_connect, cli);

        /* clear the password after client_connect, assume that nothing else
         * will need to hook it and use it anymore. */
        if (cp != NULL) {
            /* clean out their password */
            if (cp->pass != NULL) {
                free(cp->pass);
                cp->pass = NULL;
            }
        }

        /* accounting stuff */
        if (CLIENT_UNKNOWN(cli))
        {
            cli->flags &= ~IRCD_CLIENT_UNKNOWN;
            ircd.stats.serv.unkclients--;
        }
        ircd.stats.serv.curclients++;
        if (ircd.stats.serv.maxclients < ircd.stats.serv.curclients)
            ircd.stats.serv.maxclients++;

        /* now welcome our client */
        sendto_one(cli, RPL_FMT(cli, RPL_WELCOME), ircd.network_full,
                cli->nick, cli->user, cli->host);
        sendto_one(cli, RPL_FMT(cli, RPL_YOURHOST), ircd.me->name,
                ircd.version);
        sendto_one(cli, RPL_FMT(cli, RPL_CREATED), ircd.ascstart);
        sendto_one(cli, RPL_FMT(cli, RPL_MYINFO), ircd.me->name, ircd.version,
                ircd.umodes.avail, ircd.cmodes.avail);
        send_isupport(cli);

    } else
        /* remote clients won't have been added to the hash, do that now */
        hash_insert(ircd.hashes.client, cli);
                                        
    ircd.stats.net.visclients++; /* clients are visible by default */
    ircd.stats.net.curclients++;
    if (ircd.stats.net.maxclients < ircd.stats.net.curclients)
        ircd.stats.net.maxclients++;

    /* If the IP hasn't been explicitly filled in yet, set it to 0.0.0.0.  Ugh
     * ugh ugh. :) */
    if (*cli->ip == '\0')
        strcpy(cli->ip, "0.0.0.0");

    cli->flags |= IRCD_CLIENT_REGISTERED;

    /* now introduce the client to our servers down the line, sptr should be
     * the server the client was introduced from (possibly us) */
    LIST_FOREACH(cp, ircd.connections.servers, lp) {
        if (cp->srv != sptr)
            cp->proto->register_user(cp,  cli);
    }

    /* hook for all clients */
    hook_event(ircd.events.register_client, cli);
    return 0;
}

int check_nickname(char *nick) {
    char first[2] = { '\0', '\0' };

    first[0] = *nick;
    if (*nick == '\0' || !istr_okay(ircd.maps.nick_first, first) ||
        !istr_okay(ircd.maps.nick, nick))
        return 0;
    return 1;
}

/* this takes a mask of just about any form as input and tries to turn it into
 * something useable in the nick!user@host form.  It mangles the input, so be
 * careful. */
char *make_client_mask(char *mask) {
    static char fmask[NICKLEN + USERLEN + HOSTLEN + 3];
    char *s, *nick, *user, *host;

    s = mask;
    nick = user = host = NULL;
    if ((user = strchr(mask, '!')) != NULL) {
        *user++ = '\0'; /* find the user portion .. */
        nick = s; /* it is nick!user.. format */
    }
    if ((host = strchr((user != NULL ? user : s), '@')) != NULL) {
        *host++ = '\0'; /* find the host portion */
        if (user == NULL) /* it is user@host form */
            user = s; /* so s is the user */
    } else if (user == NULL && strpbrk(s, ".:") != NULL)
        host = s; /* it is a plain host */
    else
        nick = s; /* it is a plain nickname */

    /* okay, we now have nick, user, and host.  make a mask. */
    sprintf(fmask, "%s!%s@%s",
            (nick == NULL || *nick == '\0' ? "*" : nick),
            (user == NULL || *user == '\0' ? "*" : user),
            (host == NULL || *host == '\0' ? "*" : host));

    return fmask;
}

int client_check_access(client_t *from, client_t *to, char *ext, event_t *ep) {
    struct client_check_args cca;

    cca.from = from;
    cca.to = to;
    cca.extra = ext;

    return hook_cond_event(ep, &cca);
}

/* mode goodies below here */
uint64_t usermode_request(unsigned char suggested, unsigned char *actual,
        int flags, int sflag, char *changer) {
    struct usermode *md = NULL;
    uint64_t allmodes = 0;
    int i;
    unsigned char c;

    /* build a list of all our currently in-use modes (some modes are
     * inaccessible, so only do it for modes which have been initialized and
     * are unavailable */
    for (i = 0;i < 256;i++) {
        if (!ircd.umodes.modes[i].avail && ircd.umodes.modes[i].mask)
            allmodes |= ircd.umodes.modes[i].mask;
    }

    if (ircd.umodes.modes[suggested].avail)
        md = &ircd.umodes.modes[suggested];
    else {
        if (islower(suggested))
            c = toupper(suggested);
        else
            c = tolower(suggested);

        if (ircd.umodes.modes[c].avail)
            md = &ircd.umodes.modes[c];
        else {
            for (i = 0;i < 256;i++) {
                if (ircd.umodes.modes[i].avail && ircd.umodes.modes[i].mask) {
                    md = &ircd.umodes.modes[i];
                    break;
                }
            }

            /* *still* NULL?  nothing free */
            if (md == NULL)
                return 0;
        }
    }

    md->avail = 0; /* mark it used */
    md->flags = flags;
    if (changer != NULL)
        md->changer = import_symbol(changer);
    else
        md->changer = NULL;
    md->sflag = sflag;
    *actual = md->mode;

    allmodes |= md->mask;

    /* rebuild our mode string */
    strcpy(ircd.umodes.avail, usermode_getstr(allmodes, 0) + 1);
    
    return md->mask;
}

void usermode_release(unsigned char mode) {
    uint64_t allmodes = 0;
    int i;

    /* same as above, see what is available to rebuild the modestring */
    for (i = 0;i < 256;i++) {
        if (!ircd.umodes.modes[i].avail && ircd.umodes.modes[i].mask)
            allmodes |= ircd.umodes.modes[i].mask;
    }
    
    /* mark our released mode available, and remove it from the potential list
     * of modes */
    ircd.umodes.modes[mode].avail = 1;
    ircd.umodes.modes[mode].flags = 0;
    ircd.umodes.modes[mode].changer = NULL;
    allmodes &= ~ircd.umodes.modes[mode].mask;

    strcpy(ircd.umodes.avail, usermode_getstr(allmodes, 0) + 1);
}

unsigned char *usermode_getstr(uint64_t modes, char global) {
    int i;
    int si; /* index to string */
    static unsigned char string[66];

    string[0] = '+'; /* prefix with a + */

    for (i = 0, si = 1;i < 256;i++) {
        if (modes & ircd.umodes.modes[i].mask &&
                (!global || ircd.umodes.modes[i].flags & USERMODE_FL_GLOBAL))
            string[si++] = ircd.umodes.modes[i].mode;
    }
    string[si] = '\0';

    return string;
}

uint64_t usermode_getmask(unsigned char *str, char global) {
    uint64_t mask = 0;
    unsigned char *s = str;

    while (*s) {
        if (!global || ircd.umodes.modes[*s].flags & USERMODE_FL_GLOBAL)
           mask |= ircd.umodes.modes[*s++].mask;
        else
            s++;
    }
    

    return mask;
}

void usermode_diff(uint64_t old, uint64_t new, char *result, char global) {
    int i;
    char *s = result;

    /* first see if they set anything */
    *s++ = '+';
    for (i = 0;i < 256;i++) {
        /* if this is a valid mode, see if it changed positively */
        if ((!global || ircd.umodes.modes[i].flags & USERMODE_FL_GLOBAL) &&
                !(old & ircd.umodes.modes[i].mask) && 
                (new & ircd.umodes.modes[i].mask))
            *s++ = ircd.umodes.modes[i].mode;
    }
    /* see if any changes were done positively, if not, dike the '+' */
    if (*(s - 1) == '+')
        s = s - 1;
    /* now see if they unset anything */
    *s++ = '-';
    for (i = 0;i < 256;i++) {
        /* if this is a valid mode, see if it changed negatively */
        if ((!global || ircd.umodes.modes[i].flags & USERMODE_FL_GLOBAL) &&
                (old & ircd.umodes.modes[i].mask) && 
                !(new & ircd.umodes.modes[i].mask))
            *s++ = ircd.umodes.modes[i].mode;
    }
    /* see if any changes were done negatively, if not, dike the '-' as done
     * above. */
    if (*(s - 1) == '-')
        s = s - 1;

    *s = '\0'; /* and terminate. */
}

int usermode_set(unsigned char mode, client_t *cli, client_t *on, char *arg,
        int *argused) {
    struct usermode *md = &ircd.umodes.modes[mode];

    if (md->avail || !md->mask)
        return 0;

    if (on->modes & md->mask)
        return 1; /* they already did it... */

    /* always call the set function.  if they're not our client, we ignore the
     * return value. */
    if ((md->changer != NULL &&
                !((usermode_func)getsym(md->changer))(cli, on, mode, 1, arg,
                    argused)) && MYCLIENT(on))
        return 0;
    /* if they're local, not opered, and the mode is an oper mode, don't let
     * them set it. */
    if (MYCLIENT(on) && !OPER(on) && md->flags & USERMODE_FL_OPER)
        return 0; /* no no. */

    /* okay, so, it's fine then, add away */
    on->modes |= md->mask;

    /* if the mode has a send flag, add them to the group for it (force the add
     * no matter what the settings for the sflag) */
    if (md->sflag > -1 && MYCLIENT(on))
        add_to_send_flag(md->sflag, on, true);

    return 1;
}


int usermode_unset(unsigned char mode, client_t *cli, client_t *on, char *arg,
        int *argused) {
    struct usermode *md = &ircd.umodes.modes[mode];

    if (md->avail || !md->mask)
        return 0;

    if (!(on->modes & md->mask))
        return 1; /* they're not set to that mode */

    /* always call the unset function.  if they're not our client, we ignore
     * the return value. */
    if ((md->changer != NULL &&
                !((usermode_func)getsym(md->changer))(cli, on, mode, 0, arg,
                    argused)) && MYCLIENT(on))
        return 0;

    /* approved for removal */
    on->modes &= ~md->mask;

    /* if the mode has a send flag, remove them from the group for it */
    if (md->sflag > -1 && MYCLIENT(on))
        remove_from_send_flag(md->sflag, on, true);

    return 1;
}

static void client_remove_history(client_t *);

/* this function adds a client to the client_history list.  If we've maxed out
 * on entries (we use the client_history hash table to examine this) we start
 * back at the first entry and overwrite our way forward again. */
struct client_history *client_add_history(client_t *cli) {
    struct client_history *chp = NULL;

    /* only one history entry may point to a client at any time */
    if (cli->hist != NULL)
        client_remove_history(cli);

    if ((chp = client_find_history(cli->nick)) != NULL)
        /* I was having this not create a new history structure, but I've had
         * so many problems with this code (little bugs here and there) that
         * I've decided to go for the slightly less efficient route.  I'm tired
         * of trying little tricks in this code to keep it fast. */
        client_remove_history(chp->cli);

   if ((hashtable_count(ircd.hashes.client_history) >=
             hashtable_size(ircd.hashes.client_history)) &&
            (hashtable_size(ircd.hashes.client_history) >=
             hashtable_size(ircd.hashes.client))) {
        /* this is the tricky one.  our table is full so we need to
         * overwrite old entries (maybe).  if the client table is larger
         * than the history table then we also let the history table grow,
         * but if the two are the same size we need to pop the oldest
         * entry off. */

        chp = TAILQ_LAST(ircd.lists.client_history, client_history_list);
        client_remove_history(chp->cli);
    }

    chp = calloc(1, sizeof(struct client_history));

    /* fill it in ... */
    strcpy(chp->nick, cli->nick);
    strcpy(chp->serv, cli->server->name);
    chp->cli = cli;
    cli->hist = chp;
    chp->signoff = me.now;

    /* insert it in the hash */
    hash_insert(ircd.hashes.client_history, chp);
    TAILQ_INSERT_HEAD(ircd.lists.client_history, chp, lp);

    return chp;
}

/* remove the client history data for a client.  if the client structure is
 * marked as 'signed off' we also free the client structure as well. */
static void client_remove_history(client_t *cli) {

    if (cli->hist == NULL) {
        log_error("trying to delete history for a client which has none.");
        return;
    }

    assert(cli == cli->hist->cli);

    hash_delete(ircd.hashes.client_history, cli->hist);
    TAILQ_REMOVE(ircd.lists.client_history, cli->hist, lp);
    free(cli->hist);
    
    if (CLIENT_HISTORY(cli))
        destroy_client(cli, NULL);
    else
        cli->hist = NULL;
}

/* this function attempts to find the client with the given nick, first by
 * looking for the actual client, then by looking in history for it.  it
 * returns an online client structure or NULL, no matter what. */
client_t *client_get_history(char *nick, time_t limit) {
    struct client_history *chp;
    client_t *cli;

    if ((cli = find_client(nick)) != NULL)
        return cli;

    /* no luck?  try the client history. */
    chp = client_find_history(nick);
    /* if we found it and they signed off at or after the current time minus
     * the time limit, return the entry. */
    if (chp != NULL && !CLIENT_HISTORY(chp->cli) &&
            chp->signoff >= (limit ? me.now - limit : 0))
        return chp->cli;

    return NULL;
}

char **client_mdext_iter(char **last) {
    client_t *cp = *(client_t **)last;
    static int started = 0;

    if (cp == NULL) {
        if (started) {
            started = 0;
            return NULL;
        }
        cp = LIST_FIRST(ircd.lists.clients);
        started = 1;
    }
    if (cp == NULL)
        return NULL;

    *(client_t **)last = LIST_NEXT(cp, lp);
    return (char **)&cp->mdext;
}

int nickcmp(char *one, char *two, size_t len __UNUSED) {
        return istrcmp(ircd.maps.nick, one, two);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
