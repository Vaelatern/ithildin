/*
 * whois.c: the WHOIS command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "commands/whois.h"

IDSTRING(rcsid, "$Id: whois.c 780 2006-10-02 01:30:16Z wd $");

MODULE_REGISTER("$Rev: 780 $");
/*
@DEPENDENCIES@: ircd
*/

event_t *whois_event;

MODULE_LOADER(whois) {

    whois_event = create_event(EVENT_FL_NORETURN);

    /* now create numerics */
#define RPL_WHOISUSER 311
    CMSG("311", "%s %s %s * :%s");
    CMSG("312", "%s %s :%s");
#define RPL_WHOISOPERATOR 313
    CMSG("313", "%s :is an IRC operator");
#define RPL_WHOISIDLE 317
    CMSG("317", "%s %ld %ld :seconds idle, signon time");
#define RPL_ENDOFWHOIS 318
    CMSG("318", "%s :End of /WHOIS list.");
#define RPL_WHOISCHANNELS 319
    CMSG("319", "%s :%s");
    CMSG("338", "%s :is actually %s@%s [%s]");

    return 1;
}
MODULE_UNLOADER(whois) {

    destroy_event(whois_event);

    DMSG(RPL_WHOISUSER);
    DMSG(RPL_WHOISSERVER);
    DMSG(RPL_WHOISOPERATOR);
    DMSG(RPL_WHOISIDLE);
    DMSG(RPL_ENDOFWHOIS);
    DMSG(RPL_WHOISCHANNELS);
    DMSG(RPL_WHOISACTUALLY);
}

/* argv[1] = nick to /whois (or place to whois from if argc > 2)
 * argv[2] = nick to request data for */
CLIENT_COMMAND(whois, 1, 2, 0) {
    client_t *target;
    char *nick = (argc > 2 ? argv[2] : argv[1]);
    struct chanlink *clp;
#define WHOISBUFLEN 320
    char buf[WHOISBUFLEN];
    int see;
    int len = 0;

    if (argc > 2 && pass_command(cli, NULL, "WHOIS", "%s %s", argc, argv, 1) !=
            COMMAND_PASS_LOCAL)
        return COMMAND_WEIGHT_HIGH;

    target = find_client(nick);
    if (target == NULL) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOSUCHNICK), nick);
        sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHOIS), nick);
        return COMMAND_WEIGHT_LOW;
    }

    /* so, just, uh, fire off the /whois */
    sendto_one(cli, RPL_FMT(cli, RPL_WHOISUSER), target->nick, target->user,
            target->host, target->info);
    /* show them the real host if they can see it and this is either a local
     * real client (conn != NULL) or orighost points somewhere different */
    if (CAN_SEE_REAL_HOST(cli, target) && target->orighost != target->host)
        sendto_one(cli, RPL_FMT(cli, RPL_WHOISACTUALLY), target->nick,
                target->user, target->orighost, target->ip);
    if (!CLIENT_MASTER(target) || MYCLIENT(target)) {
        /* don't show channels that master clients are in unless this is a
         * query directly to the master server. */
        len = 0;
        LIST_FOREACH(clp, &target->chans, lpcli) {
            /* send on potential overflow */
            if (len + ircd.limits.chanlen + 16 >= WHOISBUFLEN) {
                buf[len - 1] = '\0';
                sendto_one(cli, RPL_FMT(cli, RPL_WHOISCHANNELS), target->nick,
                        buf);
                len = 0;
            }
            
            see = can_can_see_channel(cli, clp->chan);
            if (see == CHANNEL_CHECK_OVERRIDE)
                len += snprintf(buf + len, WHOISBUFLEN - len, "%%%s%s ",
                        chanmode_getprefixes(clp->chan, target),
                        clp->chan->name);
            else if (see < 0)
                len += snprintf(buf + len, WHOISBUFLEN - len, "%s%s ",
                        chanmode_getprefixes(clp->chan, target),
                        clp->chan->name);
        }
        if (len) {
            buf[len - 1] = '\0';
            sendto_one(cli, RPL_FMT(cli, RPL_WHOISCHANNELS), target->nick, buf);
        }
    }
    sendto_one(cli, RPL_FMT(cli, RPL_WHOISSERVER), target->nick,
            target->server->name, target->server->info);
    if (OPER(target))
        sendto_one(cli, RPL_FMT(cli, RPL_WHOISOPERATOR), target->nick);

    /* hook the 'whois' event, the hook functions will know who issued the
     * whois by using the 'cptr' global. */
    hook_event(whois_event, target);

    if (MYCLIENT(target)) {
        /* if it's our client, send idle info too */
        sendto_one(cli, RPL_FMT(cli, RPL_WHOISIDLE), target->nick,
                me.now - target->last, target->signon);
    }
    sendto_one(cli, RPL_FMT(cli, RPL_ENDOFWHOIS), target->nick);

    return COMMAND_WEIGHT_MEDIUM;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
