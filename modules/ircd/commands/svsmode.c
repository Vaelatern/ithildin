/*
 * svsmode.c: the SVSMODE command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/servicesid.h"
#include "commands/mode.h"

IDSTRING(rcsid, "$Id: svsmode.c 754 2006-06-24 19:02:03Z wd $");

MODULE_REGISTER("$Rev: 754 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/servicesid
@DEPENDENCIES@: ircd/commands/mode
*/

static void do_svsmode(client_t *, server_t *, int, char **);
/* the client/server commands both operate in the same manner, they send one
 * of two messages:
 * SVSMODE nick [ts mode|mode [args]]
 * SVSMODE channel [modes [args...]]
 * Services really shouldn't send channel modes at all.  Oh well. */
CLIENT_COMMAND(svsmode, 2, 0, 0) {

    if (!CLIENT_MASTER(cli))
        return COMMAND_WEIGHT_NONE;

    do_svsmode(cli, NULL, argc, argv);

    return COMMAND_WEIGHT_NONE;
}
SERVER_COMMAND(svsmode, 2, 0, 0) {

    if (!SERVER_MASTER(srv))
        return 0;

    do_svsmode(NULL, srv, argc, argv);

    return 0;
}

static void do_svsmode(client_t *cli, server_t *srv, int argc, char **argv) {
    channel_t *chan;
    client_t *cp;
    char **myargv;
    int myargc;

    if (check_channame(argv[1]) && (chan = find_channel(argv[1])) != NULL) {
        /* it's a channel svsmode, go ahead and send it .. */
        channel_mode(cli, srv, chan, chan->created, argc - 2, argv + 2, 1);
    } else if ((cp = find_client(argv[1])) != NULL) {
        /* Services setting "client modes."  There's also this sort of like,
         * stupid hack where they send "+d <ts>" which is a 'servicesid'
         * used to track clients on the network.  We one-off support this
         * and treat everything else as a real client mode. */

        /* They may send a ts along first.  We must skip this. */
        if (isdigit(*argv[2])) {
            myargv = argv + 3; /* skip to argv[3] */
            myargc = argc - 3;
        } else {
            myargv = argv + 2;
            myargc = argc - 2;
        }

        if (myargc == 2 && (!strcmp(myargv[0], "+d") ||
                    !strcmp(myargv[0], "+T"))) {
            if (!strcmp(argv[0], "+d"))
                /* this is so dumb */
                SVSID(cp) = str_conv_int(myargv[1], 0);

            sendto_serv_butone(sptr, cli, srv, cp->nick, "SVSMODE",
                    "%s %s", myargv[0], myargv[1]);
            return;
        }
        if (MYCLIENT(cp))
            /* Is this our client?  If so do the usermode thing.  Otherwise
             * just pass along the command, we expect a MODE back when it is
             * processed by the user's server. */
            user_mode(NULL, cp, myargc, myargv, true);

        sendto_serv_butone(sptr, cli, srv, cp->nick, "SVSMODE", "%s",
                myargv[0]);
    }
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
