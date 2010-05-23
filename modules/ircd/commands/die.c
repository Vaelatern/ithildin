/*
 * die.c: the DIE command.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: die.c 718 2006-04-25 11:44:28Z wd $");

MODULE_REGISTER("$Rev: 718 $");
/*
@DEPENDENCIES@: ircd
*/

/* the die command, kills the server rather forcefully and violently.  of
 * course we send a message to all our users first. :)  argv[1] is the password
 * to down the server, if any, and argv[2] is an optional message. */
CLIENT_COMMAND(die, 0, 2, COMMAND_FL_OPERATOR) {
    char *diepass = NULL;
    char md5sum[33];
    connection_t *cp;
    char *msg = (argc > 2 ? argv[2] : "");
    char cmsg[512], smsg[512];

    if (cmd != NULL && cmd->conf != NULL)
        diepass = conf_find_entry("password", cmd->conf, 1);

    if (diepass != NULL) {
        if (argc < 2) {
            sendto_one(cli, RPL_FMT(cli, ERR_NEEDMOREPARAMS), argv[0]);
            return COMMAND_WEIGHT_NONE;
        }
        md5_data(argv[1], strlen(argv[1]), md5sum);
        if (strcmp(md5sum, diepass)) {
            sendto_one(cli, RPL_FMT(cli, ERR_PASSWDMISMATCH));
            return COMMAND_WEIGHT_NONE;
        }
    } else if (argc == 2)
        msg = argv[1];

    /* success, now we walk along each connection and tell them we're saying
     * goodbye. :) */
    sprintf(cmsg, "Server Terminating.  Killed by %s[%s@%s] (%s)", cli->nick,
            cli->user, cli->host, msg);
    sprintf(smsg, "Terminated by %s[%s@%s] (%s)", cli->nick, cli->user,
            cli->host, msg);
    while ((cp = LIST_FIRST(ircd.connections.clients)) != NULL) {
        sendto_one(cp->cli, "NOTICE", ":%s", cmsg);
        cp->flags |= IRCD_CLIENT_KILLED;
        if (sendq_flush(cp))
            destroy_client(cp->cli, cmsg);
    }
    while ((cp = LIST_FIRST(ircd.connections.servers)) != NULL)
        destroy_server(cp->srv, smsg);
    
    /* mark the process for shutdown.  this won't actually kill the process,
     * just tell it that it needs to go. */
    exit_process(NULL, NULL);

    return IRCD_CONNECTION_CLOSED; /* and nothing else matters! */
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
