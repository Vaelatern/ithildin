/*
 * dreamforge.c: the dreamforge server<->server protocol
 * 
 * Copyright 2003-2006 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: dreamforge.c 703 2006-03-02 13:06:55Z wd $");

MODULE_REGISTER("$Rev: 703 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/core
@DEPENDENCIES@: ircd/commands/akill        ircd/commands/gnotice
@DEPENDENCIES@: ircd/commands/services        ircd/commands/sqline
@DEPENDENCIES@: ircd/commands/svskill        ircd/commands/svsmode
@DEPENDENCIES@: ircd/commands/svsnick
*/

uint64_t protocol_flags = PROTOCOL_SFL_SHORTAKILL;

/* parser for packets */
static int packet_parse(connection_t *cp);
void setup(connection_t *);
void register_user(connection_t *, client_t *);
void sync_channel(connection_t *, channel_t *);

/* function to send PROTOCTL along (bleah!) */
HOOK_FUNCTION(dreamforge_si_hook);

MODULE_LOADER(dreamforge) {
    
    /* register a hook to send CAPABs to servers */
    add_hook(ircd.events.server_introduce, dreamforge_si_hook);

    return 1;
}
MODULE_UNLOADER(dreamforge) {

    remove_hook(ircd.events.server_introduce, dreamforge_si_hook);
}

#include "shared/rfc1459_io.c"

/* now parse buf.  buf should either be:
 * :prefix COMMAND arg1 arg2 arg3 ... :last arg[\r]\n
 * or:
 * COMMAND arg1 arg2 arg3 ... :last arg[\r]\n */
static int packet_parse(connection_t *cp) {
    char *s, *s2;
    int i;
    int client = 0;

    sptr = cp->srv; /* originates from this server */
    s = cp->buf;
    cp->stats.precv++;
    if (*s == ':') {
        s++;
        s2 = s;
        while (!isspace(*s) && *s)
            s++;
        *s++ = '\0';
        /* we've blocked out our 'sender'.  now figure out what it is.  if it
         * has a . in the name, it's a server, otherwise, it's a client.  that
         * limitation (.s must be in server names) *is* enforced. */
        if (strchr(s2, '.') != NULL)
            cptr.srv = find_server(s2);
        else {
            cptr.cli = find_client(s2);
            client = 1; /* client sent this command */
        }

        if (cptr.cli == NULL) {
           log_warn("got message with nonexistant origin %s", s2);
           return 1; /* do not process commands with unknown origins */
           /* XXX: we should probably send a KILL back?  do this when we're
            * more confident that we aren't losing people!  also, what if we
            * get an unknown origin for a server?  yech! */
        }
        
        while (isspace(*s) && *s)
            s++;
    } else
        cptr.srv = sptr; /* fill in cptr, if no prefix was given */
        
    if (*s == '\0')
        return 1;
    /* copy maxargs - 1 at most, if there is data left after the loop, copy
     * it into the last argument.  joy */
    for (i = 0;i < RFC1459_MAXARGS - 1;i++) {
        s2 = s;
        if (*s == ':') {
            strncpy(ircd.argv[i], s2 + 1, COMMAND_MAXARGLEN);
            *s = '\0'; /* so we don't trigger below */
            break;
        }
        while (!isspace(*s) && *s)
            s++;
        if (*s != '\0')
            *s++ = '\0';
        strncpy(ircd.argv[i], s2, COMMAND_MAXARGLEN);
        while (isspace(*s) && *s)
            s++;
        if (*s == '\0')
            break;
    }
    /* only copy if there is more data and we fell out of the loop */
    if (*s && i == RFC1459_MAXARGS - 1)
        strncpy(ircd.argv[i], s, COMMAND_MAXARGLEN);

    ircd.argc = i + 1;
    if (client)
        return command_exec_client(ircd.argc, ircd.argv, cptr.cli);
    return command_exec_server(ircd.argc, ircd.argv, cptr.srv);
}

void setup(connection_t *cp) {

    if (cp->buf == NULL) {
        cp->buf = malloc(RFC1459_PKT_LEN);
        cp->buflen = 0;
        cp->bufsize = RFC1459_PKT_LEN;
        memset(cp->buf, 0, cp->bufsize);
    }

    create_server(cp);
}

HOOK_FUNCTION(dreamforge_si_hook) {
    server_t *srv = (server_t *)data;

    sendto_serv_from(srv, NULL, NULL, NULL, "PROTOCTL", "NOQUIT");
    return NULL;
}

/* register a user.  not too tough. :) */
void register_user(connection_t *conn, client_t *cli) {

    sendto_serv_from(conn->srv, NULL, NULL, NULL, "NICK",
            "%s %d %d %s %s %s 0 :%s",
            cli->nick, cli->hops + 1, cli->ts, cli->user, cli->orighost,
            cli->server->name, cli->info);
    sendto_serv_from(conn->srv, cli, NULL, cli->nick, "MODE", "%s",
            usermode_getstr(cli->modes, 1));
}

/* syncing channels with dreamforge is *really* ugly.  we have to send
 * individual JOINs for each member on our side, then send the modes for
 * everything across the wire... */
void sync_channel(connection_t *conn, channel_t *chan) {
    struct chanlink *clp;
    char modes[64], *m;
#define MODEBUF_SIZE 320
    char modebuf[MODEBUF_SIZE];
    unsigned char *s;
    int optused;
    int len, cnt;
    void *state; /* state for chanmode_query */

    /* send a JOIN for each client */
    LIST_FOREACH(clp, &chan->users, lpchan) {
        if (cli_uplink(clp->cli) == conn)
            continue; /* move along */

        sendto_serv_from(conn->srv, clp->cli, NULL, chan->name, "JOIN", NULL);
    }

    /* now send modes.  this code is based somewhat on the reset code in
     * commands/mode.c */
    s = ircd.cmodes.avail;
    m = modes;
    len = 0;
    cnt = 0;
    *m++ = '+';
    modebuf[0] = '\0';
    while (*s) {
        state = NULL;
        if (ircd.cmodes.modes[*s].flags & CHANMODE_FL_PREFIX) {
            /* a prefix-type mode, walk the channel users list and see who has
             * this mode, then send along the buffer! */
            LIST_FOREACH(clp, &chan->users, lpchan) {
                if (cli_uplink(clp->cli) == conn)
                    continue;

                if (chanlink_ismode(clp, *s)) {
                    *m++ = *s;
                    cnt++;
                    len = strlcat(modebuf, clp->cli->nick, MODEBUF_SIZE);
                    modebuf[len++] = ' ';
                    modebuf[len] = '\0';

                    if (cnt == 6 ||
                            MODEBUF_SIZE - len <= ircd.limits.nicklen + 1)  {
                        *m = '\0';
                        sendto_serv(conn->srv, "MODE", "%s %s %s %d",
                                chan->name, modes, modebuf, chan->created);
                        m = modes + 1;
                        len = 0;
                        modebuf[0] = '\0';
                        cnt = 0;
                    }
                }
            }
        }
        optused = MODEBUF_SIZE - len - 2;
        while (chanmode_query(*s, chan, modebuf + len, &optused, &state) ==
                CHANMODE_OK) {
            if (optused < 0) {
                *m = '\0';
                sendto_serv(conn->srv, "MODE", "%s %s %s %d", chan->name,
                        modes, modebuf, chan->created);
                m = modes + 1;
                modebuf[0] = '\0';
                len = cnt = 0;
                optused = MODEBUF_SIZE - 2;
                continue;
            }
            *m++ = *s;
            cnt++;
            if (optused) {
                len += optused;
                modebuf[len++] = ' '; /* append a space */
                modebuf[len] = '\0';
            }
            if (cnt == 6) {
                *m = '\0';
                sendto_serv(conn->srv, "MODE", "%s %s %s %d", chan->name,
                        modes, modebuf, chan->created);
                m = modes + 1;
                modebuf[0] = '\0';
                len = cnt = 0;
                optused = MODEBUF_SIZE - 2;
            }
            optused = MODEBUF_SIZE - len - 2;
        }
        s++;
    }
    /* send off any spares. */
    if (cnt) {
        *m = '\0';
        sendto_serv(conn->srv, "MODE", "%s %s %s %d", chan->name,
                modes, modebuf, chan->created);
    }
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
