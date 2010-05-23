/*
 * quit.c: the QUIT command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: quit.c 748 2006-06-03 23:25:00Z wd $");

MODULE_REGISTER("$Rev: 748 $");
/*
@DEPENDENCIES@: ircd
*/

static int quit_format;
MODULE_LOADER(quit) {

    quit_format = create_message("user-quit-message", "%s");
    return 1;
}
MODULE_UNLOADER(quit) {

    destroy_message(quit_format);
}

/*
 * argv[1] ?= quit message
 */
CLIENT_COMMAND(quit, 0, 1, COMMAND_FL_UNREGISTERED|COMMAND_FL_REGISTERED) {
    char *msg = argc > 1 ? argv[1] : "";
    char fmsg[TOPICLEN + 1];
    struct chanlink *clp, *clp2;

    if (MYCLIENT(cli)) {
        snprintf(fmsg, TOPICLEN, MSG_FMT(cli, quit_format), msg);
        fmsg[TOPICLEN] = '\0';

        /* We check to see if their message would be moderated in any channels
         * they are in.  If this is the case they are parted from the channels
         * before the quit is sent. */
        if (!CLIENT_MASTER(cli) && *fmsg != '\0') {
            clp = LIST_FIRST(&cli->chans);
            while (clp != NULL) {
                clp2 = LIST_NEXT(clp, lpcli);

                if (can_can_send_channel(cli, clp->chan, fmsg) >= 0) {
                    sendto_channel_local(clp->chan, cli, NULL, "PART", NULL);
                    sendto_serv_butone(sptr, cli, NULL, clp->chan->name,
                            "PART", NULL);
                    del_from_channel(cli, clp->chan, true);
                }
                clp = clp2;
            }
        }

        /* Be sure to return appropriately for our local clients */
        destroy_client(cli, fmsg);
        return IRCD_CONNECTION_CLOSED;
    } else {
        destroy_client(cli, msg);
        return COMMAND_WEIGHT_NONE;
    }
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
