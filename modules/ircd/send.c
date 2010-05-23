/*
 * send.c: various functions used for sending messages
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * A considerable amount of routines are defined in this file: the send queue
 * routines are first, making it easy to queue messages for users.  Following
 * that are various routines for sending messages to one or several targets
 * independent of protocol.  Finally, there are support functions for
 * maintaining groups of formatted messages at the end of the file.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: send.c 849 2010-04-30 01:59:07Z wd $");

/* prototypes */
void set_message_in_set(int num, message_set_t *msp, char *def, char *sus);
static inline void sendto_common(connection_t *cp, client_t *cli,
        server_t *srv, char *cmd, char *to, char *msg, va_list vl);

/*****************************************************************************
 * sendq section here                                                        *
******************************************************************************/ 
/* oookay, here's how sendq works:
 * each connection has a 'sendq' variable, each sendq variable is actually a
 * list header for 'sendq_item' variables (which are unique per user!).
 * Each sendq_item variable points to a sendq_block (which is what contains
 * the actual data).  In this way, we can allocate a single copy of a
 * message for each message sent to multiple places, and simply link it into
 * peoples' send queues.  In the majority of code, you will only ever need
 * to use the functions provided below, and won't run into the middleman
 * structure (sendq_item) much at all. */

/* create a new sendq block.  this creates a block with zero references, and
 * the given message and length */
struct sendq_block *create_sendq_block(char *msg, int len) {
    struct sendq_block *bp = malloc(sizeof(struct sendq_block));

    bp->msg = malloc(len);
    bp->len = len;
    bp->refs = 0;

    memcpy(bp->msg, msg, len);

    return bp;
}

/* these allow you to add/remove sendq blocks. push adds the given block to
 * the end of the list and increments ref.  pop takes off the first item
 * (make sure you are done with it!), decrements ref, and if ref is zero,
 * does the various freeing necessary */
void sendq_push(struct sendq_block *bp, connection_t *cp) {
    struct sendq_item *sip = malloc(sizeof(struct sendq_item));
    sip->block = bp;
    sip->offset = 0;

    bp->refs++;
    if (STAILQ_FIRST(&cp->sendq) == NULL)
        STAILQ_INSERT_HEAD(&cp->sendq, sip, lp);
    else
        STAILQ_INSERT_TAIL(&cp->sendq, sip, lp);

    cp->sendq_items++;
}
/* this will almost certainly result in a core if sendq_pop is called when
 * there is no sendq.  assume this risk at the benefit of speed */
void sendq_pop(connection_t *cp) {
    struct sendq_item *sip = STAILQ_FIRST(&cp->sendq);
    struct sendq_block *bp = sip->block;

    STAILQ_REMOVE_HEAD(&cp->sendq, lp); /* remove the first entry */
    free(sip);

    bp->refs--;
    if (bp->refs == 0) {
        free(bp->msg);
        free(bp);
    }
    cp->sendq_items--;
}

/*****************************************************************************
 * send function section here                                                *
******************************************************************************/ 

/* these four functions are used to determine in what direction a client or
 * server lies.  *_uplink will return the connection (if any) whereas
 * *_server_uplink will return the server connected to us. */
connection_t *cli_uplink(client_t *cli) {
    
    if (cli == NULL)
        return NULL;

    if (MYCLIENT(cli))
        return cli->conn;
    return srv_uplink(cli->server);
}
server_t *cli_server_uplink(client_t *cli) {
    
    if (cli == NULL)
        return NULL;

    if (MYCLIENT(cli))
        return ircd.me;
    return srv_server_uplink(cli->server);
}

connection_t *srv_uplink(server_t *srv) {

    if (srv == ircd.me || srv == NULL)
        return NULL; /* if it's us, return NULL */

    while (srv->parent != ircd.me && srv->parent != NULL)
        srv = srv->parent;

#ifdef DEBUG_CODE
    if (!MYSERVER(srv)) {
        log_warn("send_find_uplink found a server who's parent is us but is "
                "not a local connection!");
        return NULL;
    }
#endif
    return srv->conn;
}
server_t *srv_server_uplink(server_t *srv) {

    if (srv == ircd.me || srv == NULL)
        return NULL; /* if it's us, return NULL */

    while (srv->parent != ircd.me && srv->parent != NULL)
        srv = srv->parent;

    return srv;
}

/* this macro is used below to clear out temporary structures after doing a
 * round of sends. */
#define CLEAR_SEND_TEMPS() do {                                                \
    protocol_t *_pp;                                                        \
    LIST_FOREACH(_pp, ircd.lists.protocols, lp) {                        \
        _pp->tmpmsg = NULL;                                                \
    }                                                                        \
    memset(ircd.sends, 0, maxsockets);                                        \
} while (0)

#define CACHE_MSG(proto) (!(proto)->flags & PROTOCOL_MFL_NOCACHE)
/* this function is used by several consumers, below, to send a message to a
 * single connection without any kind of coalescing involved. */
static inline void sendto_common(connection_t *cp, client_t *cli,
        server_t *srv, char *cmd, char *to, char *msg, va_list vl) {
    struct protocol_sender ps = {cli, srv};
    struct send_msg *sm = NULL;

    if (cp == NULL)
        return; /* don't send if there's no connection handy */

    sm = cp->proto->output(&ps, cmd, to, msg, vl);
    sendto(cp, sm->msg, sm->len);
}

/* while a lot of these could be better done as macros, until I can rely on
 * having a C99 compiler, I have to leave them as functions unfortunately. */

/* send a message from ourselves to a single client,  do not use this for
 * servers, only clients.  use sendto_serv for servers */
void sendto_one(client_t *cp, char *cmd, char *msg, ...) {
    va_list vl;
    va_start(vl, msg);
    sendto_common(cli_uplink(cp), NULL, ircd.me, cmd, cp->nick, msg, vl);
    va_end(vl);
}

/* send a message from another client to a single client, same dynamics as
 * above */
void sendto_one_from(client_t *cp, client_t *cli, server_t *srv, char *cmd,
        char *msg, ...) {
    va_list vl;
    va_start(vl, msg);
    sendto_common(cli_uplink(cp), cli, srv, cmd, cp->nick, msg, vl);
    va_end(vl);
}
/* send a message from another client to a single client with a specified
 * target. */
void sendto_one_target(client_t *cp, client_t *cli, server_t *srv, char *to,
        char *cmd, char *msg, ...) {
    va_list vl;
    va_start(vl, msg);
    sendto_common(cli_uplink(cp), cli, srv, cmd, to, msg, vl);
    va_end(vl);
}

/* send a message from ourselves to a single server, this does protocol
 * formatting, and if the server is not local to us, will handle sending it in
 * the right direction.  Also, when we send to servers, we don't specify a
 * target.  why?  who knows! */
void sendto_serv(server_t *sp, char *cmd, char *msg, ...) {
    va_list vl;
    va_start(vl, msg);
    sendto_common(srv_uplink(sp), NULL, ircd.me, cmd, NULL, msg, vl);
    va_end(vl);
}

/* sends a message from some other source to a single server, works like the
 * above, a 'to' target is also provided, so you can specify who/what you're
 * sending to, or specify NULL if you have no specific target */
void sendto_serv_from(server_t *sp, client_t *cli, server_t *srv, char *to,
        char *cmd, char *msg, ...) {
    va_list vl;
    va_start(vl, msg);
    sendto_common(srv_uplink(sp), cli, srv, cmd, to, msg, vl);
    va_end(vl);
}

/* sends a message from some other source to all the servers but 'one', one can
 * be NULL if you want to send to all servers, or can be ourself */
void sendto_serv_butone(server_t *one, client_t *cli, server_t *srv, char *to,
        char *cmd, char *msg, ...) {
    struct protocol_sender ps = {cli, srv};
    struct send_msg *sm = NULL;
    connection_t *conn, *ones;
    va_list vl;


    /* walk our server list */
    ones = srv_uplink(one);
    LIST_FOREACH(conn, ircd.connections.servers, lp) {
        if (conn == ones || conn == NULL)
            continue; /* skip 'one' */

        if (conn->proto->tmpmsg == NULL)
        {
            va_start(vl, msg);
            sm = conn->proto->output(&ps, cmd, to, msg, vl);
            va_end(vl);
        }
        if (CACHE_MSG(conn->proto)) {
            conn->proto->tmpmsg = create_sendq_block(sm->msg, sm->len);
            sendq_push(conn->proto->tmpmsg, conn);
        } else
            sendq_push(create_sendq_block(sm->msg, sm->len), conn);
    }

    CLEAR_SEND_TEMPS();
}

/* Just like above, except it only sends it to servers with a matching flag (or
 * without the matching flag is the 'match' argument is false. */
void sendto_serv_pflag_butone(uint64_t flag, bool pos, server_t *one,
        client_t *cli, server_t *srv, char *to, char *cmd, char *msg, ...) {
    struct protocol_sender ps = {cli, srv};
    struct send_msg *sm = NULL;
    connection_t *conn, *ones;
    va_list vl;

    /* walk our server list */
    ones = srv_uplink(one);
    LIST_FOREACH(conn, ircd.connections.servers, lp) {
        if (conn == ones ||
                (pos && !SERVER_SUPPORTS(conn->srv, flag)) ||
                (!pos && SERVER_SUPPORTS(conn->srv, flag)))
            continue; /* skip 'one' and non-matching servers */

        if (conn->proto->tmpmsg == NULL)
        {
            va_start(vl, msg);
            sm = conn->proto->output(&ps, cmd, to, msg, vl);
            va_end(vl);
        }
        if (CACHE_MSG(conn->proto)) {
            conn->proto->tmpmsg = create_sendq_block(sm->msg, sm->len);
            sendq_push(conn->proto->tmpmsg, conn);
        } else
            sendq_push(create_sendq_block(sm->msg, sm->len), conn);
    }

    CLEAR_SEND_TEMPS();
}

/* sends a message to everyone in a channel (including sender) */
void sendto_channel(channel_t *chan, client_t *cli, server_t *srv, char *cmd,
        char *msg, ...) {
    struct protocol_sender ps = {cli, srv};
    struct send_msg *sm = NULL;
    struct chanlink *cp;
    connection_t *conn;
    va_list vl;
        
    /* walk the channel list, for remote users, only pass the message to their
     * server, expect the server to propogate among its uplinks.  make sure to
     * only send the message once, too! */
    LIST_FOREACH(cp, &chan->users, lpchan) {
        conn = cli_uplink(cp->cli);
        if (ircd.sends[conn->sock->fd] || conn == NULL)
            continue;
        else
            ircd.sends[conn->sock->fd] = 1;

        if (conn->proto->tmpmsg == NULL)
        {
            va_start(vl, msg);
            sm = conn->proto->output(&ps, cmd, chan->name, msg, vl);
            va_end(vl);
        }
        if (CACHE_MSG(conn->proto)) {
            conn->proto->tmpmsg = create_sendq_block(sm->msg, sm->len);
            sendq_push(conn->proto->tmpmsg, conn);
        } else
            sendq_push(create_sendq_block(sm->msg, sm->len), conn);
    }

    CLEAR_SEND_TEMPS();
}
        
/* sends a message to everyone in a channel (including sender) who is on this
 * server. */
void sendto_channel_local(channel_t *chan, client_t *cli, server_t *srv,
        char *cmd, char *msg, ...) {
    struct protocol_sender ps = {cli, srv};
    struct send_msg *sm = NULL;
    struct chanlink *cp;
    connection_t *conn;
    va_list vl;
        
    /* walk the channel list, only send to our own clients. */
    LIST_FOREACH(cp, &chan->users, lpchan) {
        if (!MYCLIENT(cp->cli))
            continue;

        conn = cli_uplink(cp->cli);
        if (conn == NULL)
            continue;

        if (conn->proto->tmpmsg == NULL)
        {
            va_start(vl, msg);
            sm = conn->proto->output(&ps, cmd, chan->name, msg, vl);
            va_end(vl);
        }
        if (CACHE_MSG(conn->proto)) {
            conn->proto->tmpmsg = create_sendq_block(sm->msg, sm->len);
            sendq_push(conn->proto->tmpmsg, conn);
        } else
            sendq_push(create_sendq_block(sm->msg, sm->len), conn);
    }

    CLEAR_SEND_TEMPS();
}

/* sends a message to every server which has users in the channel except the
 * server sending the message */
void sendto_channel_remote(channel_t *chan, client_t *cli, server_t *srv,
        char *cmd, char *msg, ...) {
    struct protocol_sender ps = {cli, srv};
    struct send_msg *sm = NULL;
    struct chanlink *cp;
    connection_t *conn;
    va_list vl;
        
    /* walk the channel list, only send to our own clients. */
    LIST_FOREACH(cp, &chan->users, lpchan) {
        if (MYCLIENT(cp->cli))
            continue;

        conn = cli_uplink(cp->cli);
        if (conn == NULL)
            continue; /* skip this one */
        if (ircd.sends[conn->sock->fd])
            continue;
        else
            ircd.sends[conn->sock->fd] = 1;

        if (conn->proto->tmpmsg == NULL)
        {
            va_start(vl, msg);
            sm = conn->proto->output(&ps, cmd, chan->name, msg, vl);
            va_end(vl);
        }
        if (CACHE_MSG(conn->proto)) {
            conn->proto->tmpmsg = create_sendq_block(sm->msg, sm->len);
            sendq_push(conn->proto->tmpmsg, conn);
        } else
            sendq_push(create_sendq_block(sm->msg, sm->len), conn);
    }

    CLEAR_SEND_TEMPS();
}

/* sends a message to everyone on a channel but 'one' */
void sendto_channel_butone(channel_t *chan, client_t *one, client_t *cli,
        server_t *srv, char *cmd, char *msg, ...) {
    struct protocol_sender ps = {cli, srv};
    struct send_msg *sm = NULL;
    struct chanlink *cp;
    connection_t *conn, *onec;
    va_list vl;
        
    /* just like above, but skip 'one'.  ideally, when I can pass varargs with
     * a #define, this function will mostly disappear, and simply flag one of
     * fd bits in ircd.sends.  Pining for C99! (XXX) */
    onec = cli_uplink(one);
    LIST_FOREACH(cp, &chan->users, lpchan) {
        conn = cli_uplink(cp->cli);
        if (conn == onec || conn == NULL)
            continue; /* skip this one */
        if (ircd.sends[conn->sock->fd])
            continue;
        else
            ircd.sends[conn->sock->fd] = 1;

        if (conn->proto->tmpmsg == NULL)
        {
            va_start(vl, msg);
            sm = conn->proto->output(&ps, cmd, chan->name, msg, vl);
            va_end(vl);
        }
        if (CACHE_MSG(conn->proto)) {
            conn->proto->tmpmsg = create_sendq_block(sm->msg, sm->len);
            sendq_push(conn->proto->tmpmsg, conn);
        } else
            sendq_push(create_sendq_block(sm->msg, sm->len), conn);
    }

    CLEAR_SEND_TEMPS();
}

/* sends a message to everyone on a channel with one of the given prefixes but
 * 'one' */
void sendto_channel_prefixes_butone(channel_t *chan, client_t *one,
        client_t *cli, server_t *srv, unsigned char *prefixes, char *cmd,
        char *msg, ...) {
    struct protocol_sender ps = {cli, srv};
    struct send_msg *sm = NULL;
    struct chanlink *cp;
    connection_t *conn, *onec;
    short pmask = 0; /* prefix mask.  compiled below */
    char pname[512]; /* prefixed name. */
    va_list vl;
        
    sprintf(pname, "%s%s", prefixes, chan->name);
    while (*prefixes) {
        struct chanmode *cmp = ircd.cmodes.pfxmap[*prefixes];
        if (cmp != NULL)
            pmask |= cmp->umask;
        prefixes++;
    }

    onec = cli_uplink(one);
    LIST_FOREACH(cp, &chan->users, lpchan) {
        conn = cli_uplink(cp->cli);
        if (conn == onec || conn == NULL)
            continue; /* skip this one */
        if (ircd.sends[conn->sock->fd])
            continue;
        else if (!pmask && cp->flags)
            continue; /* only unprefixed users */
        else if (pmask && !(cp->flags & pmask))
            continue; /* only users which match part of this prefix */
        else
            ircd.sends[conn->sock->fd] = 1;

        if (conn->proto->tmpmsg == NULL)
        {
            va_start(vl, msg);
            sm = conn->proto->output(&ps, cmd, pname, msg, vl);
            va_end(vl);
        }
        if (CACHE_MSG(conn->proto)) {
            conn->proto->tmpmsg = create_sendq_block(sm->msg, sm->len);
            sendq_push(conn->proto->tmpmsg, conn);
        } else
            sendq_push(create_sendq_block(sm->msg, sm->len), conn);
    }

    CLEAR_SEND_TEMPS();
}

/* send a message to the common channels of the connection's client
 * (obviously, servers aren't in channels).  This includes sending to the
 * user.  If you're curious as to what this is good for (I know I was when I
 * saw it in ircd), basically, two things: QUIT and NICK commands.  This
 * beast is also the reason protocols have to support having no target.  Also,
 * we only send to *LOCAL USERS* in the channel.  when propogating things like
 * QUIT/NICK, you must call a separate sendto_serv* function to propogate this
 * data to other servers, who should then propogate it to these channels. */
void sendto_common_channels(client_t *cli, server_t *srv, char *cmd,
        char *msg, ...) {
    struct protocol_sender ps = {cli, srv};
    struct send_msg *sm = NULL;
    struct chanlink *chanp, *userp;
    connection_t *conn;
    va_list vl;
        
    /* for each channel the user is in, walk the list and do sends.  it would
     * be nice to use 'sendto_channel' here, but because it clears the temps
     * when it finishes, that's not an option.  we don't want users receiving
     * dupes!  also, the message sent is not targeted at all.  this function is
     * basically for NICK and QUIT commands, and nothing else. */
    if (LIST_FIRST(&cli->chans) == NULL) {
        /* if they're not in a channel, and they're my client, send them the
         * message anyhow, if they're not my client, don't do anything */
        if (MYCLIENT(cli) && !ircd.sends[cli->conn->sock->fd])
        {
            va_start(vl, msg);
            sendto_common(cli->conn, cli, srv, cmd, NULL, msg, vl);
            va_end(vl);
        }
    } else {
        LIST_FOREACH(chanp, &cli->chans, lpcli) {
            LIST_FOREACH(userp, &chanp->chan->users, lpchan) {
                if (!MYCLIENT(userp->cli))
                    continue; /* only send to local clients */

                conn = cli_uplink(userp->cli);
                if (conn == NULL)
                    continue;
                if (ircd.sends[conn->sock->fd])
                    continue; /* already sent this way */

                ircd.sends[conn->sock->fd] = 1;
                if (conn->proto->tmpmsg == NULL)
                {
                    va_start(vl, msg);
                    sm = conn->proto->output(&ps, cmd, NULL, msg, vl);
                    va_end(vl);
                }
                if (CACHE_MSG(conn->proto)) {
                    conn->proto->tmpmsg = create_sendq_block(sm->msg, sm->len);
                    sendq_push(conn->proto->tmpmsg, conn);
                } else
                    sendq_push(create_sendq_block(sm->msg, sm->len), conn);
            }
        }
    }

    CLEAR_SEND_TEMPS();
}

/* sends a message to all clients/servers matching the given pattern.
 * #pattern is hostnames (client only), $pattern is servers. */
void sendto_match_butone(client_t *one, client_t *cli, server_t *srv,
        char *mask, char *cmd, char *msg, ...) {
    struct protocol_sender ps = {cli, srv};
    struct send_msg *sm = NULL;
    int host = 0; /* 1 if hostmask, 0 if servermask */
    char *pat = mask + 1;
    client_t *cp;
    connection_t *conn;
    va_list vl;
        
    if (*pat == '#') {
        /* a hostmask */
        host = 1;
        pat++;
    }

    /* walk the list of clients for the network, do matches as necessary.  a
     * lot of matches for non-local clients are luckily pre-empted if we're
     * already sending along to the uplink.  this ensures also that only the
     * necessary servers receive the data */
    LIST_FOREACH(cp, ircd.lists.clients, lp) {
        if (cp == one)
            continue;
        if (!CLIENT_REGISTERED(cp))
            continue;

        conn = cli_uplink(cp);
        if (conn == NULL)
            continue; /* pseudo-client */
        if (ircd.sends[conn->sock->fd])
            continue; /* already sent this way */
        if ((host ? !match(pat, cp->host) : !match(pat, cp->server->name)))
            continue; /* not a match */
        ircd.sends[conn->sock->fd] = 1;

        if (conn->proto->tmpmsg == NULL)
        {
            va_start(vl, msg);
            sm = conn->proto->output(&ps, cmd, mask, msg, vl);
            va_end(vl);
        }
        if (CACHE_MSG(conn->proto)) {
            conn->proto->tmpmsg = create_sendq_block(sm->msg, sm->len);
            sendq_push(conn->proto->tmpmsg, conn);
        } else
            sendq_push(create_sendq_block(sm->msg, sm->len), conn);
    }

    CLEAR_SEND_TEMPS();
}

/* sends a message to a group of users in the given chanusers structure.
 * useful for sending messages to everyone set a certain mode. */
void sendto_group(struct chanusers *group, int target, client_t *cli,
        server_t *srv, char *cmd, char *msg, ...) {
    struct chanlink *cp;
    va_list vl;
        
    /* just walk the list.  we shouldn't have any remote users if this is being
     * used for usermodes.  other consumers be wary. */
    LIST_FOREACH(cp, group, lpchan) {
        if (!MYCLIENT(cp->cli))
            continue; /* only local.. */
        if (cp->cli->conn == NULL)
            continue;

        va_start(vl, msg);
        sendto_common(cp->cli->conn, cli, srv, cmd,
                (target ? cp->cli->nick : NULL), msg, vl);
        va_end(vl);
    }

    /* we don't touch the temp stuff here. */
}
/*****************************************************************************
 * message flags                                                            *
 *****************************************************************************/

/* create a new send flag with the given name and return the number to
 * reference it with back to the caller.  also, set the group to the given
 * group so that the caller can handle it as they will.  if 'flags' is
 * negative, assume it is a privilege (when the negative is inverted. */
int create_send_flag(char *name, int flags, int priv) {
    int i;

    if (name == NULL || *name == '\0')
        return -1; /* ..grunt */

    for (i = 0;i < ircd.sflag.size ; i++) {
        if (ircd.sflag.flags[i].num == -1)
            break;
    }
    if (i == ircd.sflag.size) {
        ircd.sflag.size++;
        ircd.sflag.flags = realloc(ircd.sflag.flags,
                sizeof(struct send_flag) * ircd.sflag.size);
    }

    ircd.sflag.flags[i].name = strdup(name);
    LIST_INIT(&ircd.sflag.flags[i].users);
    ircd.sflag.flags[i].priv = priv;
    ircd.sflag.flags[i].flags = flags;
    return (ircd.sflag.flags[i].num = i);
}

/* a quick function to find the flag number associated with the given name.
 * if nothing is found, this returns -1 */
int find_send_flag(char *name) {
    int i;

    for (i = 0;i < ircd.sflag.size;i++) {
        if (!strcasecmp(ircd.sflag.flags[i].name, name))
            return i;
    }

    return -1;
}

void destroy_send_flag(int flg) {
    struct chanlink *clp;

    while ((clp = LIST_FIRST(&ircd.sflag.flags[flg].users)) != NULL) {
        LIST_REMOVE(clp, lpchan);
        free(clp);
    }
    free(ircd.sflag.flags[flg].name);

    memset(&ircd.sflag.flags[flg], 0, sizeof(struct send_flag));
    ircd.sflag.flags[flg].num = -1;
}

int add_to_send_flag(int flg, client_t *cli, bool force) {
    struct chanlink *clp;

    if (!MYCLIENT(cli)) {
        log_warn("add_to_send_flag(%d, %s, %d) called with non-local client!",
                flg, cli->nick, force);
        return -1;
    }

    if (find_in_send_flag(flg, cli) != NULL)
        return -1; /* already there. */

    if (force == false && 
            ((ircd.sflag.flags[flg].priv > -1 &&
              !BPRIV(cli, ircd.sflag.flags[flg].priv)) ||
             (ircd.sflag.flags[flg].flags & SEND_LEVEL_OPERATOR &&
              !OPER(cli))))
        return ERR_NOPRIVILEGES;

    /* otherwise, put them in. */
    clp = calloc(1, sizeof(struct chanlink));
    clp->cli = cli;
    clp->chan = NULL;
    LIST_INSERT_HEAD(&ircd.sflag.flags[flg].users, clp, lpchan);

    return 0;
}

struct chanlink *find_in_send_flag(int flg, client_t *cli) {
    struct chanlink *clp;

    LIST_FOREACH(clp, &ircd.sflag.flags[flg].users, lpchan) {
        if (clp->cli == cli)
            return clp;
    }

    return NULL;
}

void remove_from_send_flag(int flg, client_t *cli, bool force) {
    struct chanlink *clp = find_in_send_flag(flg, cli);

    if (force == false &&
            ircd.sflag.flags[flg].flags & SEND_LEVEL_CANTCHANGE)
        return; /* er, no.. */

    if (clp != NULL) {
        LIST_REMOVE(clp, lpchan);
        free(clp);
    }
}

/* this functions sends a message to all users in the given message flag from
 * the current server.  Unlike the other 'sendto_' functions, we format the
 * message specially here.  It is always a NOTICE command and it is always
 * prefixed with '*** Notice -- '.   Also, the messages are always targeted
 * directly at the user. */
void sendto_flag(int flg, char *msg, ...) {
    struct chanlink *clp;
    char lmsg[512];
    va_list vl;

    if (flg < 0 || flg >= ircd.sflag.size || ircd.sflag.flags[flg].num < 0)
        return; /* nothing to do here. */

    snprintf(lmsg, 512, ":*** Notice -- %s", msg);
    /* now just walk down the list of users and send the message off. */
    LIST_FOREACH(clp, &ircd.sflag.flags[flg].users, lpchan) {
        if (clp->cli->conn == NULL)
            continue;

        va_start(vl, msg);
        sendto_common(clp->cli->conn, NULL, ircd.me, "NOTICE", clp->cli->nick,
                lmsg, vl);
        va_end(vl);
    }
}

/* Just like above, but use privileges as a limiter too. */
void sendto_flag_priv(int flg, int priv, bool pos, char *msg, ...) {
    struct chanlink *clp;
    char lmsg[512];
    va_list vl;
        
    if (flg < 0 || flg >= ircd.sflag.size || ircd.sflag.flags[flg].num < 0)
        return; /* nothing to do here. */

    snprintf(lmsg, 512, ":*** Notice -- %s", msg);
    /* now just walk down the list of users and send the message off. */
    LIST_FOREACH(clp, &ircd.sflag.flags[flg].users, lpchan) {
        if (clp->cli->conn == NULL)
            continue;
        if ((pos && !BPRIV(clp->cli, priv)) ||
                (!pos && BPRIV(clp->cli, priv)))
            continue;

        va_start(vl, msg);
        sendto_common(clp->cli->conn, NULL, ircd.me, "NOTICE", clp->cli->nick,
                lmsg, vl);
        va_end(vl);
    }
}

/* this is like the above, but a bit more complicated.  First of all, we use a
 * slightly tweaked message format: '*** Notice -- %s from %s: %s', from is
 * grabbed from the cli/srv arguments, and type and message are specified in
 * the arguments.  Otherwise, it's just like sendto_flag(). */
void sendto_flag_from(int flg, client_t *cli, server_t *srv, char *type,
        char *msg, ...) {
    struct chanlink *clp;
    char lmsg[512];
    va_list vl;

    if (flg < 0 || flg >= ircd.sflag.size || ircd.sflag.flags[flg].num < 0)
        return; /* nothing to do here. */

    snprintf(lmsg, 512, ":*** %s -- from %s: %s", type,
            (cli != NULL ? cli->nick : srv->name), msg);
    /* now just walk down the list of users and send the message off. */
    LIST_FOREACH(clp, &ircd.sflag.flags[flg].users, lpchan) {
        if (clp->cli->conn == NULL)
            continue;

        /* note that we always send from ourselves still!  this isn't a
         * mistake. */
        va_start(vl, msg);
        sendto_common(clp->cli->conn, NULL, ircd.me, "NOTICE", clp->cli->nick,
                lmsg, vl);
        va_end(vl);
    }
}

/*****************************************************************************
 * message section here                                                      *
 *****************************************************************************/ 

/* sets a specific message in the given set, and does error checking (this
 * is done in a couple of places, so the duplicated work gets to be done in
 * a separate function) */
void set_message_in_set(int num, message_set_t *msp, char *def, char *sus) {
    char *s;

    if (sus != NULL) {
        s = (char *)fmtcheck(sus, def);
        if (s != sus)
            log_warn("In message-set %s, for message %s: invalid format "
                    "\"%s\", should match \"%s\"", msp->name,
                    ircd.messages.msgs[num].name, sus, def);
    } else
        s = def;
    if (*s != '\0')
        msp->msgs[num] = strdup(s);
}

/* this creates a new message set with the given name out of the given conf,
 * and fills it in with either custom data from conf, or the defaults for each
 * message. */
message_set_t *create_message_set(char *name, conf_list_t *conf) {
    message_set_t *msp;
    int i;
    char *ent;

    msp = find_message_set(name);
    if (msp != NULL) {
        log_debug("updating message set %s", name);
        /* clear out the old array */
        for (i = 0;i < ircd.messages.count;i++) {
            if (ircd.messages.msgs[i].num <= 0)
                continue;
            free(msp->msgs[i]);
        }
        /* and remove them from ze list */
        LIST_REMOVE(msp, lp);
    } else {
        msp = malloc(sizeof(message_set_t));
        msp->name = strdup(name);
        msp->msgs = malloc(sizeof(char *) * ircd.messages.size);
    }

    for (i = 0;i < ircd.messages.count;i++) {
        if (ircd.messages.msgs[i].num <= 0)
            continue;
        ent = conf_find_entry(ircd.messages.msgs[i].name, conf, 1);
        set_message_in_set(i, msp, ircd.messages.msgs[i].default_fmt, ent);
    }

    if (LIST_FIRST(ircd.messages.sets) == NULL)
        LIST_INSERT_HEAD(ircd.messages.sets, msp, lp);
    else
        LIST_INSERT_AFTER(LIST_FIRST(ircd.messages.sets), msp, lp);
    return msp;
}

/* this is a small/simple function used to find message sets. */
message_set_t *find_message_set(char *name) {
    message_set_t *msp;

    LIST_FOREACH(msp, ircd.messages.sets, lp) {
        if (!strcasecmp(msp->name, name))
            return msp;
    }
    return NULL;
}
                
/* this is, currently, a dangerous function to call.  we don't know what-all
 * refers to our current set, and as such we should probably not just do
 * this.  */
void destroy_message_set(message_set_t *set) {
    int i;

    free(set->name);
    for (i = 0;i < ircd.messages.count;i++) {
        if (set->msgs[i] != NULL)
            free(set->msgs[i]);
    }

    LIST_REMOVE(set, lp);
    free(set);
}

/* this function creates a new message type with the given name and given
 * default format.  It is erroneous to not have a default format, as the
 * default format is used when checking user formats.  Empty strings and
 * NULL pointers will be rejected and -1 will be returned, otherwise, this
 * function always succeeds.  Additionally, if the command is a three-digit
 * integer, it will be placed in the array in the proper place (numerically).
 * After creating a message, it should be referred to by *number*, as seen
 * below.  For convenience, the first 1000 entries in the array always
 * belong to their respective numerics, whether they exist or not.  Finally,
 * when a new message is added, all existing message sets are updated with
 * either the new default format, or, if it is in the conf, the format supplied
 * by the user */
int create_message(char *name, char *fmt) {
    message_t mp;
    message_set_t *msp;
    conf_list_t *clp;
    int i;

    if (fmt == NULL || *fmt == '\0' || name == NULL || *name == '\0')
        return -1;

    mp.num = find_message(name);
    if (mp.num != -1) {
        log_warn("tried to create a message which already exists. (%s)",
                name);
        return -1;
    }

    mp.name = strdup(name);
    mp.default_fmt = strdup(fmt);

    /* is it a numeric? */
    if (strlen(name) == 3 && isdigit(*name) && isdigit(*(name + 1)) &&
            isdigit(*(name + 2))) {
        mp.num = atoi(name);
        ircd.messages.msgs[mp.num] = mp;
    } else {
        /* it's not a numeric, do allocating the hard way */
        if (ircd.messages.count == ircd.messages.size) {
            /* loop through the messages and try and find a hole.  remember to
             * start at 1000 (past the numerics) */
            for (i = 1000;i < ircd.messages.count;i++) {
                if (ircd.messages.msgs[i].num == -1) {
                    mp.num = i;
                    break;
                }
            }
            if (i == ircd.messages.count) {
                ircd.messages.size += 512; /* allocate 512 more slots */
                ircd.messages.msgs = realloc(ircd.messages.msgs,
                        sizeof(message_t) * ircd.messages.size);
                /* dirty hack here */
                memset(ircd.messages.msgs + (sizeof(message_t) *
                            (ircd.messages.size - 512)), 0,
                        sizeof(message_t) * 512);

                /* re-allocate the arrays for our message sets, too */
                LIST_FOREACH(msp, ircd.messages.sets, lp) {
                    msp->msgs = realloc(msp->msgs, sizeof(char *) *
                            ircd.messages.size);
                    memset(msp->msgs + (sizeof(char *) *
                                (ircd.messages.size - 512)),
                            0, sizeof(char *) * 512);
                }
                mp.num = ircd.messages.count++;
            }
        } else
            mp.num = ircd.messages.count++; /* use the next number, and
                                               increment count */
        memcpy(&ircd.messages.msgs[mp.num], &mp, sizeof(message_t));
    }

    /* okay, message inserted into the array.  but it's not that easy.  we
     * have to go around and set either the configured message or the default
     * message in all of our sets.  yuck! */
    LIST_FOREACH(msp, ircd.messages.sets, lp) {
        conf_entry_t *cep = conf_find("message-set", NULL, CONF_TYPE_LIST,
                *ircd.confhead, 1);
        char *ent;

        if (cep != NULL)
            clp = cep->list;
        else
            clp = NULL;

        if (clp != NULL)
            ent = conf_find_entry(mp.name, clp, 1);
        else
            ent = NULL;

        set_message_in_set(mp.num, msp, mp.default_fmt, ent);
    }

    return mp.num;
}

/* this function destroys a message with the given number.  it should probably
 * only be called if the message is never ever going to be referenced again. */
void destroy_message(int num) {
    struct message_set *msp;

    free(ircd.messages.msgs[num].name);
    ircd.messages.msgs[num].name = NULL;
    ircd.messages.msgs[num].num = -1;
    free(ircd.messages.msgs[num].default_fmt);
    ircd.messages.msgs[num].default_fmt = NULL;

    /* clear out the message in all the sets, too. */
    LIST_FOREACH(msp, ircd.messages.sets, lp) {
        free(msp->msgs[num]);
        msp->msgs[num] = NULL;
    }
}

/* find a message with the given name.  this isn't a particularly fast
 * operation, and as such should *NOT* be used often (really probably only
 * in create/destroy_message...but) */
int find_message(char *name) {
    int i;

    for (i = 0; i < ircd.messages.count;i++) {
        if (ircd.messages.msgs[i].name != NULL &&
                !strcasecmp(ircd.messages.msgs[i].name, name))
            return i;
    }
        
    return -1;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
