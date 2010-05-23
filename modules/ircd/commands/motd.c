/*
 * motd.c: the MOTD command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 *
 * The MOTD command/system provides a way to specify MOTD and short MOTD by
 * class, as well as to specify the default MOTD for clients.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: motd.c 736 2006-05-30 04:52:22Z wd $");

MODULE_REGISTER("$Rev: 736 $");
/*
@DEPENDENCIES@: ircd
*/

static struct {
    char motd[PATH_MAX];
    char smotd[PATH_MAX];
} motd;

HOOK_FUNCTION(motd_rc_hook);
static void motd_send(client_t *, char *);

MODULE_LOADER(motd) {

    snprintf(motd.motd, PATH_MAX, "%s/ircd/motd", me.conf_path);
    snprintf(motd.smotd, PATH_MAX, "%s/ircd/smotd", me.conf_path);

    add_hook(ircd.events.register_client, motd_rc_hook);

    /* numerics .. */
#define RPL_MOTD 372
    CMSG("372", ":- %s");
#define RPL_MOTDSTART 375
    CMSG("375", ":- %s Message of the Day - ");
#define RPL_ENDOFMOTD 376
    CMSG("376", ":End of /MOTD command.");
#define ERR_NOMOTD 422
    CMSG("422", ":MOTD file is missing.");

    return 1;
}
MODULE_UNLOADER(motd) {

    remove_hook(ircd.events.register_client, motd_rc_hook);

    DMSG(RPL_MOTD);
    DMSG(RPL_MOTDSTART);
    DMSG(RPL_ENDOFMOTD);
    DMSG(ERR_NOMOTD);
}

/* the MOTD command.  we decide what class the user is in and then dump the
 * file to them if it exists. */
CLIENT_COMMAND(motd, 0, 1, COMMAND_FL_REGISTERED) {
    class_t *cls = (cli->conn != NULL ? cli->conn->cls :
            LIST_FIRST(ircd.lists.classes));
    char *fname;

    if (pass_command(cli, NULL, "MOTD", "%s", argc, argv, 1) !=
            COMMAND_PASS_LOCAL)
        return COMMAND_WEIGHT_EXTREME; /* remote MOTDs are considered spendy */

    if ((fname = conf_find_entry("motd", cls->conf, 1)) == NULL)
        fname = motd.motd;

    motd_send(cli, fname);

    return COMMAND_WEIGHT_HIGH;
}

HOOK_FUNCTION(motd_rc_hook) {
    client_t *cli = (client_t *)data;
    class_t *cls = (cli->conn != NULL ? cli->conn->cls : NULL);
    char *fname;

    if (cls != NULL) {
        if ((fname = conf_find_entry("short-motd", cls->conf, 1)) == NULL)
            fname = motd.smotd;
        motd_send(cli, fname);
    }

    return NULL;
}

static void motd_send(client_t *cli, char *file) {
    FILE *fp = fopen(file, "r");
    char buf[256];

    if (fp != NULL) {
        sendto_one(cli, RPL_FMT(cli, RPL_MOTDSTART), ircd.me->name);
        while ((sfgets(buf, 256, fp)) != NULL)
            sendto_one(cli, RPL_FMT(cli, RPL_MOTD), buf);
        sendto_one(cli, RPL_FMT(cli, RPL_ENDOFMOTD));
        fclose(fp);
    } else
        sendto_one(cli, RPL_FMT(cli, ERR_NOMOTD));
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
