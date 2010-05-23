/*
 * lusers.c: the LUSERS command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: lusers.c 668 2006-01-16 21:20:53Z wd $");

MODULE_REGISTER("$Rev: 668 $");
/*
@DEPENDENCIES@: ircd
*/

HOOK_FUNCTION(lusers_rc_hook);

MODULE_LOADER(lusers) {

    add_hook(ircd.events.register_client, lusers_rc_hook);

#define RPL_LUSERCLIENT 251
    CMSG("251", ":There are %d users and %d invisible on %d servers");
#define RPL_LUSEROP 252
    CMSG("252", "%d :IRC operators online");
#define RPL_LUSERUNKNOWN 253
    CMSG("253", "%d :unknown connection(s)");
#define RPL_LUSERCHANNELS 254
    CMSG("254", "%d :channels formed");
#define RPL_LUSERME 255
    CMSG("255", ":I have %d clients and %d servers");
#define RPL_LOCALUSERS 265
    CMSG("265", ":Current local users: %d Max: %d");
#define RPL_GLOBALUSERS 266
    CMSG("266", ":Current global users: %d Max: %d");

    return 1;
}
MODULE_UNLOADER(lusers) {

    remove_hook(ircd.events.register_client, lusers_rc_hook);

    DMSG(RPL_LUSERCLIENT);
    DMSG(RPL_LUSEROP);
    DMSG(RPL_LUSERUNKNOWN);
    DMSG(RPL_LUSERCHANNELS);
    DMSG(RPL_LUSERME);
    DMSG(RPL_LOCALUSERS);
    DMSG(RPL_GLOBALUSERS);
}

/* argv[1] == server to build stats for (XXX: not imp)
 * argv[2] == place to request data from */
CLIENT_COMMAND(lusers, 0, 2, COMMAND_FL_REGISTERED) {

    if (pass_command(cli, NULL, "LUSERS", "%s %s", argc, argv, 2) !=
            COMMAND_PASS_LOCAL)
        return 0;
    
    sendto_one(cli, RPL_FMT(cli, RPL_LUSERCLIENT), ircd.stats.net.visclients,
            ircd.stats.net.curclients - ircd.stats.net.visclients,
            ircd.stats.servers);
    if (ircd.stats.opers)
        sendto_one(cli, RPL_FMT(cli, RPL_LUSEROP), ircd.stats.opers);
    if (ircd.stats.serv.unkclients)
        sendto_one(cli, RPL_FMT(cli, RPL_LUSERUNKNOWN),
                ircd.stats.serv.unkclients);
    if (ircd.stats.channels)
        sendto_one(cli, RPL_FMT(cli, RPL_LUSERCHANNELS), ircd.stats.channels);
    sendto_one(cli, RPL_FMT(cli, RPL_LUSERME), ircd.stats.serv.curclients,
            ircd.stats.serv.servers);
    sendto_one(cli, RPL_FMT(cli, RPL_LOCALUSERS), ircd.stats.serv.curclients,
            ircd.stats.serv.maxclients);
    sendto_one(cli, RPL_FMT(cli, RPL_GLOBALUSERS), ircd.stats.net.curclients,
            ircd.stats.net.maxclients);

    return COMMAND_WEIGHT_LOW;
}

/* we hook on register_clients so that the server statistics are all correct
 * when we issue the info.  If the client isn't local, of course, we don't send
 * them the data. :) */
HOOK_FUNCTION(lusers_rc_hook) {
    client_t *cli = (client_t *)data;

    if (MYCLIENT(cli) && cli->conn != NULL)
        c_lusers_cmd(NULL, 1, NULL, cli);

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
