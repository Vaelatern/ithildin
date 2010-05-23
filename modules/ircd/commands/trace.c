/*
 * trace.c: the TRACE command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: trace.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

MODULE_LOADER(trace) {

    /* I don't think all of these numerics are used anymore, still.. */
#define RPL_TRACELINK 200
    CMSG("200", "Link %s %s %s");
#define RPL_TRACECONNECTING 201
    CMSG("201", "Attempt %d %s");
#define RPL_TRACEHANDSHAKE 202
    CMSG("202", "Handshaking %d %s");
#define RPL_TRACEUNKNOWN 203
    CMSG("203", "???? %s %s %d");
#define RPL_TRACEOPERATOR 204
    CMSG("204", "Operator %s %s %ld");
#define RPL_TRACEUSER 205
    CMSG("205", "User %s %s %ld");
#define RPL_TRACESERVER 206
    CMSG("206", "Server %s %dS %dC %s %s %ld");
#define RPL_TRACECLASS 209
    CMSG("209", "Class %s %d");
#define RPL_ENDOFTRACE 262
    CMSG("262", "%s :End of /TRACE listing.");

    return 1;
}
MODULE_UNLOADER(trace) {

    DMSG(RPL_TRACELINK);
    DMSG(RPL_TRACECONNECTING);
    DMSG(RPL_TRACEHANDSHAKE);
    DMSG(RPL_TRACEUNKNOWN);
    DMSG(RPL_TRACEOPERATOR);
    DMSG(RPL_TRACEUSER);
    DMSG(RPL_TRACESERVER);
    DMSG(RPL_TRACECLASS);
    DMSG(RPL_ENDOFTRACE);
}

/* argv[1] ?= server/client to trace to */
CLIENT_COMMAND(trace, 0, 2, COMMAND_FL_REGISTERED) {
    client_t *cp = NULL;
    server_t *sp = NULL;
    connection_t *connp;
    class_t *clsp;
    char *target = (argc > 1 ? argv[1] : ircd.me->name);

    if (argc > 1 && ((cp = find_client(target)) != NULL ||
                (sp = find_server(target)) != NULL)) {
        if (!CAN_SEE_SERVER(cli, (cp != NULL ? cp->server : sp))) {
            sendto_one(cli, RPL_FMT(cli, ERR_NOPRIVILEGES));
            return COMMAND_WEIGHT_LOW;
        }
    }
        
    /* don't show link info in the double argument case unless the command was
     * destined for us.  in this case we may actually pass the command on again
     * if the client isn't on our server, but.. */
    if (argc > 2)
        if (pass_command(cli, NULL, "TRACE", "%s %s", argc, argv, 2) !=
                COMMAND_PASS_LOCAL)
            return COMMAND_WEIGHT_HIGH;

    switch (pass_command(cli, NULL, "TRACE", "%s", argc, argv, 1)) {
        case COMMAND_PASS_REMOTE:
            if (cp != NULL)
                sp = cli_server_uplink(cp);
            else
                sp = srv_server_uplink(sp);
            sendto_one(cli, RPL_FMT(cli, RPL_TRACELINK), ircd.version, argv[1],
                    sp->name);
            return COMMAND_WEIGHT_HIGH;
        case COMMAND_PASS_LOCAL:
            break; /* we'll continue below */
        default:
            return COMMAND_WEIGHT_NONE; /* uhm.. */
    }

    /* a trace to us.  the trace may specify a target, a pattern, or nothing at
     * all (if it was local) */
    sendto_flag(SFLAG("SPY"), "TRACE requested by %s (%s@%s) [%s]", cli->nick,
            cli->user, cli->host, cli->server->name);

    /* non-operator traces must specify an exact client.  other traces might do
     * this too.  if you remember we found cp up above. ;) */
    if (!OPER(cli) || cp != NULL) {
        if (cp != NULL) {
            /* found a client.  Send the info */
            if (OPER(cp))
                sendto_one(cli, RPL_FMT(cli, RPL_TRACEOPERATOR),
                        (cp->conn != NULL ? cp->conn->cls->name : "*"),
                        cp->nick,
                        (cp->conn != NULL ? me.now - cp->conn->last : 0));
            else
                sendto_one(cli, RPL_FMT(cli, RPL_TRACEUSER),
                        (cp->conn != NULL ? cp->conn->cls->name : "*"),
                        cp->nick,
                        (cp->conn != NULL ? me.now - cp->conn->last : 0));
        }
        sendto_one(cli, RPL_FMT(cli, RPL_ENDOFTRACE), target);
        return COMMAND_WEIGHT_HIGH; /* non-operators don't go any further than
                                       this. */
    }

    /* okay, we know they're an oper, and we know they're tracing something
     * that isn't a client (most likely the server itself).  Right now I'm just
     * going to nerf wildcard support. */
    /* for now, don't send a full dump of clients on the server.  this seems
     * fairly valueless, especially on big servers, and there are other ways to
     * get full listings.  Only dump unknowns/opers/servers/classes for now */
    LIST_FOREACH(connp, ircd.connections.stage1, lp)
        sendto_one(cli, RPL_FMT(cli, RPL_TRACEUNKNOWN), connp->cls->name,
                connp->host, me.now - connp->last);
    LIST_FOREACH(connp, ircd.connections.stage2, lp)
        sendto_one(cli, RPL_FMT(cli, RPL_TRACEUNKNOWN), connp->cls->name,
                connp->host, me.now - connp->last);
    LIST_FOREACH(connp, ircd.connections.clients, lp) {
        cp = connp->cli;
        if (OPER(cp))
            sendto_one(cli, RPL_FMT(cli, RPL_TRACEOPERATOR), connp->cls->name,
                    cli->nick, me.now - connp->last);
    }
    LIST_FOREACH(connp, ircd.connections.servers, lp) {
        /* XXX: ye gods is this expensive. */
        int cnt = 0, scnt = 0; /* client/server count */
        LIST_FOREACH(cp, ircd.lists.clients, lp)
            if (cli_server_uplink(cp) == connp->srv)
                cnt++;
        LIST_FOREACH(sp, ircd.lists.servers, lp)
            if (sp == connp->srv || srv_server_uplink(sp) == connp->srv)
                scnt++;
        sp = connp->srv;
        sendto_one(cli, RPL_FMT(cli, RPL_TRACESERVER), connp->cls->name, cnt,
                scnt, sp->name, "*", me.now - connp->last);
    }
    LIST_FOREACH(clsp, ircd.lists.classes, lp) {
        if (clsp->clients > 0)
            sendto_one(cli, RPL_FMT(cli, RPL_TRACECLASS), clsp->name,
                    clsp->clients);
    }
    sendto_one(cli, RPL_FMT(cli, RPL_ENDOFTRACE), target);

    return COMMAND_WEIGHT_NONE;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
