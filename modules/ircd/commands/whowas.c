/*
 * whowas.c: the WHOWAS command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "commands/whois.h"

IDSTRING(rcsid, "$Id: whowas.c 737 2006-05-30 05:02:34Z wd $");

MODULE_REGISTER("$Rev: 737 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/commands/whois
*/

MODULE_LOADER(whowas) {

#define RPL_WHOWASUSER 314
    CMSG("314", "%s %s %s * :%s");
#define RPL_ENDOFWHOWAS 369
    CMSG("369", "%s :End of /WHOWAS");
#define ERR_WASNOSUCHNICK 406
    CMSG("406", "%s :There was no such nickname");

    return 1;
}
MODULE_UNLOADER(whowas) {
    DMSG(RPL_WHOWASUSER);
    DMSG(RPL_ENDOFWHOWAS);
    DMSG(ERR_WASNOSUCHNICK);
}

/* the whowas command:
 * argv[1] == nick to whowas
 * argv[2] ?= max number of replies (not used here, always one)
 * argv[3] ?= server to get whowas from */
CLIENT_COMMAND(whowas, 1, 3, 0) {
    struct client_history *target;
    struct tm *signoff_tm;
    char signoff[128];

    if (argc > 3 &&
            pass_command(cli, NULL, "WHOWAS", "%s %s :%s", argc, argv, 3) !=
            COMMAND_PASS_LOCAL)
        return COMMAND_WEIGHT_HIGH;

    target = client_find_history(argv[1]);

    if (target == NULL) {
        sendto_one(cli, RPL_FMT(cli, ERR_WASNOSUCHNICK), argv[1]);
        sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHOWAS), argv[1]);
        return COMMAND_WEIGHT_LOW;
    }

    /* much less data here than in /whois */
    sendto_one(cli, RPL_FMT(cli, RPL_WHOWASUSER), target->nick,
            target->cli->user, target->cli->host, target->cli->info);
    if (CAN_SEE_REAL_HOST(cli, target->cli) &&
            target->cli->orighost != target->cli->host)
        sendto_one(cli, RPL_FMT(cli, RPL_WHOISACTUALLY), target->nick,
                target->cli->user, target->cli->orighost, target->cli->ip);
    signoff_tm = gmtime(&target->signoff);
    strftime(signoff, 128, "%a %b %d %H:%M:%S %Z %Y", signoff_tm);
    sendto_one(cli, RPL_FMT(cli, RPL_WHOISSERVER), target->nick, target->serv,
            signoff);
    sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHOWAS), target->nick);

    return COMMAND_WEIGHT_MEDIUM;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
