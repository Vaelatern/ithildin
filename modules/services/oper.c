/*
 * oper.c: operator service routines
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains the handling routines for the operator service.
 */

#include <ithildin/stand.h>

#include "services.h"

IDSTRING(rcsid, "$Id: oper.c 579 2005-08-21 06:38:18Z wd $");

static SERVICES_COMMAND(os_die);
static SERVICES_COMMAND(os_sync);

enum reply_codes {
    RPL_NOTOPER = 0,
    RPL_NOPERM,
    RPL_HELP,
    RPL_UNKNOWN_COMMAND,

    RPL_DIE,

    RPL_SYNC,

    RPL_LASTREPLY
};
static int replies[RPL_LASTREPLY];

void oper_setup(void) {

    replies[RPL_LASTREPLY] = -1;
    replies[RPL_NOTOPER] = create_message("os-notoper",
            "only IRC operators have access to this service.");
    replies[RPL_NOPERM] = create_message("os-noperm",
            "you do not have access to that function.");
    replies[RPL_HELP] = create_message("os-help",
            "for help: /operserv help %s");
    replies[RPL_UNKNOWN_COMMAND] = create_message("os-unknown-command",
            "unknown command %s.");

    replies[RPL_DIE] = create_message("os-die", "%s has told me to die! :(");

    replies[RPL_SYNC] = create_message("os-sync",
            "%s has requested a database sync.");
}


void oper_handle_msg(client_t *cli, int argc, char **argv) {

    if (!strcasecmp(argv[0], "\000PING")) {
        if (argc == 1)
            send_reply(cli, &services.oper, "%s %s", argv[0], argv[1]);
        else
            send_reply(cli, &services.oper, "%s %s %s", argv[0], argv[1],
                    argv[2]);
    }

    /* below here it's operator only stuff. */
    if (!OPER(cli)) {
        send_reply(cli, &services.oper, MSG_FMT(cli, replies[RPL_NOTOPER]));
        return;
    }
    if (!strcasecmp(argv[0], "DIE"))
        os_die(cli, argc, argv);
    else if (!strcasecmp(argv[0], "SYNC"))
        os_sync(cli, argc, argv);
    else {
        send_reply(cli, &services.oper, MSG_FMT(cli,
                    replies[RPL_UNKNOWN_COMMAND]), argv[0]);
        send_reply(cli, &services.oper, MSG_FMT(cli, replies[RPL_HELP]), "");
    }
}   

static SERVICES_COMMAND(os_die) {
    struct services_client_data *scdp =
        (struct services_client_data *)mdext(cli, services.mdext.client);

    if (scdp->nick == NULL || !regnick_admin(scdp->nick)) {
        send_reply(cli, &services.oper, MSG_FMT(cli, replies[RPL_NOPERM]));
        return;
    }

    send_opnotice(&services.oper, MSG_FMT(cli, replies[RPL_DIE]),
            cli->nick);
    exit_process(NULL, NULL);
}

static SERVICES_COMMAND(os_sync) {
    struct services_client_data *scdp =
        (struct services_client_data *)mdext(cli, services.mdext.client);

    if (scdp->nick == NULL || !regnick_admin(scdp->nick)) {
        send_reply(cli, &services.oper, MSG_FMT(cli, replies[RPL_NOPERM]));
        return;
    }

    send_opnotice(&services.oper, MSG_FMT(cli, replies[RPL_SYNC]),
            cli->nick);
    db_sync();
    mail_send();
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
