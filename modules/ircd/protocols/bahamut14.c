/*
 * bahamut14.c: the DALnet-ized server<->server protocol
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: bahamut14.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/core
@DEPENDENCIES@: ircd/commands/akill        ircd/commands/capab
@DEPENDENCIES@: ircd/commands/gnotice        ircd/commands/services
@DEPENDENCIES@: ircd/commands/sqline        ircd/commands/svskill
@DEPENDENCIES@: ircd/commands/svsmode        ircd/commands/svsnick
*/

uint64_t protocol_flags = PROTOCOL_SFL_SJOIN | PROTOCOL_SFL_NOQUIT |
    PROTOCOL_SFL_TSMODE | PROTOCOL_SFL_TS;

/* parser for packets */
static int packet_parse(connection_t *cp);
void setup(connection_t *);
void register_user(connection_t *, client_t *);
void sync_channel(connection_t *, channel_t *);

/* function to send CAPABs along */
HOOK_FUNCTION(bahamut14_si_hook);

MODULE_LOADER(bahamut14) {
    
    /* register a hook to send CAPABs to servers */
    add_hook(ircd.events.server_introduce, bahamut14_si_hook);

    return 1;
}
MODULE_UNLOADER(bahamut14) {

    remove_hook(ircd.events.server_introduce, bahamut14_si_hook);
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

HOOK_FUNCTION(bahamut14_si_hook) {
    server_t *srv = (server_t *)data;

    sendto_serv_from(srv, NULL, NULL, NULL, "CAPAB",
            "TS3 NOQUIT SSJOIN UNCONNECT NICKIP TSMODE");

    return NULL;
}

/* for registering a user.  bahamut sends a ridiculous amount of information
 * across the wire. */
void register_user(connection_t *conn, client_t *cli) {
    uint32_t addr;

    /* XXX: bahamut sends IP addresses in network-byte-order 32 bit integer
     * format.  that's great, but disregards IPv6.  A more portable format of
     * sending the IP address of a remote user should be created.  Not to
     * mention it shouldn't come with the NICK line.  Pfft.  For now, we make
     * due and do our best.  This is pretty fudged, though. */
    if (inet_pton(PF_INET, cli->ip, &addr) != 1)
        addr = 0;
    sendto_serv_from(conn->srv, NULL, NULL, NULL, "NICK",
            "%s %d %d %s %s %s %s %u 0 :%s", cli->nick, cli->hops + 1,
            cli->ts, usermode_getstr(cli->modes, 1), cli->user, cli->orighost,
            cli->server->name, addr, cli->info);
}

#define BUF_SIZE 320
#include "shared/sjoin_sync_channel.c"

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
