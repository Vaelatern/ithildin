/*
 * webirc.c: the WEBIRC command.
 * 
 * Copyright 2008 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: die.c 718 2006-04-25 11:44:28Z wd $");

MODULE_REGISTER("$Rev: 718 $");
/*
@DEPENDENCIES@: ircd
*/

/* argv[1] == password
 * argv[2] == "cgiirc"
 * argv[3] == user hostname
 * argv[4] == user ip
 * The WEBIRC command facilitates hostname masking from mibbit (and possibly
 * other) sources of IRC servers */
CLIENT_COMMAND(webirc, 4, 4, COMMAND_FL_UNREGISTERED) {
    const char *pass = NULL;
    const char *host = NULL;
    bool matched = false;
    connection_t *cp = cli->conn;

    /* we might want to toss the connection at this point...? */
    if (cmd == NULL || cmd->conf == NULL) {
        log_error("WEBIRC command called without configuration!");
        return COMMAND_WEIGHT_NONE;
    }

    if (cp == NULL) {
        log_warn("WEBIRC command from connectionless client");
        return COMMAND_WEIGHT_NONE;
    }

    if (strcasecmp(argv[2], "cgiirc")) {
        log_warn("WEBIRC command received unexpected second argument %s",
                argv[2]);
        return COMMAND_WEIGHT_NONE;
    }

    if ((pass = conf_find_entry("password", cmd->conf, 1)) == NULL) {
        log_error("No WEBIRC password specified");
        return COMMAND_WEIGHT_NONE;
    }
    /* Do a hostname check (actually do host/IP/etc) for every specified
     * host */
    while (!matched &&
           (host = conf_find_entry_next("host", host, cmd->conf, 1)) != NULL) {
        if (hostmatch(host, cp->host) || hostmatch(host, cli->ip) ||
            ipmatch(host, cli->ip))
            matched = true;
    }

    if (matched && strcmp(argv[1], pass)) {
        log_warn("Incorrect password for WEBIRC command from %s@%s",
                 (*cp->user ? cp->user : "<unknown>"), cp->host);
        matched = false;
    }

    if (matched) {
        sendto_flag(SFLAG("SPY"),
                    "Changing WebIRC hostname for %s@%s to %s[%s]",
                    (*cp->user ? cp->user : "<unknown>"),
                    cp->host, argv[3], argv[4]);
        strlcpy(cli->host, argv[3], HOSTLEN + 1);
        strlcpy(cli->ip, argv[4], IPADDR_MAXLEN + 1);
    }

    return COMMAND_WEIGHT_NONE;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
