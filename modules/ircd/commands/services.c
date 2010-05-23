/*
 * services.c: the SERVICES (and a whole bunch more) commands
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 *
 * This is intended as a quick replacement for all the goop of services
 * commands that bahamut (and df) provided.  I've hardcoded the values
 * provided, but in the future it might be desirable to let the user create
 * their own silly aliases. ;).  Some of these commands are also downright lame
 * (esp SERVICES and IDENTIFY).
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "commands/away.h"

IDSTRING(rcsid, "$Id: services.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

static char *services_host;
static char *stats_host; /* XXX: this is really silly and arbitrary */
static client_t fake_svc_client;

HOOK_FUNCTION(services_conf_hook);

MODULE_LOADER(services) {

    add_command_alias("services", "identify");
    add_command_alias("services", "chanserv");
    add_command_alias("services", "cs");
    add_command_alias("services", "helpserv");
    add_command_alias("services", "hs");
    add_command_alias("services", "memoserv");
    add_command_alias("services", "ms");
    add_command_alias("services", "nickserv");
    add_command_alias("services", "ns");
    add_command_alias("services", "operserv");
    add_command_alias("services", "os");
    add_command_alias("services", "rootserv");
    add_command_alias("services", "rs");
    add_command_alias("services", "statserv");
    add_command_alias("services", "ss");

    add_hook(me.events.read_conf, services_conf_hook);
    services_conf_hook(NULL, NULL);

    /* setup our silly fake services client */
    memset(&fake_svc_client, 0, sizeof(client_t));
    strcpy(fake_svc_client.nick, "Services");
    strcpy(fake_svc_client.user, "services");
    strlcpy(fake_svc_client.host, services_host, HOSTLEN);

#define ERR_SERVICESDOWN 440
    CMSG("440", "%s :Services is currently down.  Please wait a few moments "
            "and then try again.");
    return 1;
}
MODULE_UNLOADER(services) {

    remove_hook(me.events.read_conf, services_conf_hook);
    DMSG(ERR_SERVICESDOWN);
}

/* the command itself.  due to (silly!) legacy behavior we have to fold down
 * the arguments the user gives us.  oh well. */
CLIENT_COMMAND(services, 1, 1, COMMAND_FL_FOLDMAX) {
    char *to = NULL, *tosrv = NULL;
    char *myargv[2];
    char *s;
    client_t *target;

    if (!strcasecmp(argv[0], "services")) {
        /* XXX: this is fundamentally stupid and broken.  unless it is endowed
         * with a better understanding of services syntax, it is
         * practically..well..useless. */
        if (!strncasecmp(argv[1], "HELP", 4)) {
            sendto_one_from(cli, &fake_svc_client, NULL, "NOTICE",
                    ":For ChanServ help use /QUOTE CHANSERV HELP");
            sendto_one_from(cli, &fake_svc_client, NULL, "NOTICE",
                    ":For NickServ help use /QUOTE NICKSERV HELP");
            sendto_one_from(cli, &fake_svc_client, NULL, "NOTICE",
                    ":For MemoServ help use /QUOTE MEMOSERV HELP");
            return COMMAND_WEIGHT_MEDIUM; /* ... sheesh */
        }
        s = argv[1];
        while (isspace(*s) && *s != '\0')
            s++;
        while (!isspace(*s) && *s != '\0')
            s++;
        while (isspace(*s) && *s != '\0')
            s++;
        /* now 's' points at the second argument of the command (or nothing).
         * determine if it goes to ChanServ, or NickServ (this is SO bogus) */
        if (*s == '#')
            myargv[0] = "CHANSERV";
        else
            myargv[0] = "NICKSERV";
        myargv[1] = argv[1];
        c_services_cmd(cmd, 2, myargv, cli);

        return COMMAND_WEIGHT_LOW;
    } else if (!strcasecmp(argv[0], "identify")) {
        s = malloc(strlen(argv[1]) + 10);
        if (*argv[1] == '#')
            myargv[0] = "CHANSERV";
        else
            myargv[0] = "NICKSERV";
        sprintf(s, "IDENTIFY %s", argv[1]);
        myargv[1] = s;
        c_services_cmd(cmd, 2, myargv, cli);
        free(s);

        return COMMAND_WEIGHT_LOW;
    } else  {
        /* some other services command.  compare on the first letter (good
         * enough, I guess) */
        tosrv = services_host;
        switch (tolower(*argv[0])) {
            case 'c':
                to = "ChanServ";
                break;
            case 'h':
                to = "HelpServ";
                tosrv = stats_host;
                break;
            case 'm':
                to = "MemoServ";
                break;
            case 'n':
                to = "NickServ";
                break;
            case 'o':
                to = "OperServ";
                tosrv = stats_host;
                break;
            case 'r':
                to = "RootServ";
                break;
            case 's':
                to = "StatServ";
                tosrv = stats_host;
                break;
            default:
                to = "NickServ"; /* uh..? */
                break;
        }

        if ((target = find_client(to)) == NULL)
            sendto_one(cli, RPL_FMT(cli, ERR_SERVICESDOWN), to);
        else
            sendto_one_from(target, cli, NULL, "PRIVMSG", ":%s", argv[1]);

        return COMMAND_WEIGHT_LOW;
    }
}

HOOK_FUNCTION(services_conf_hook) {
    conf_list_t *clp;
    char *s;

    if (services_host != NULL) {
        free(services_host);
        services_host = NULL;
    }
    if (stats_host != NULL) {
        free(stats_host);
        stats_host = NULL;
    }

    if ((clp = conf_find_list("global", *ircd.confhead, 1)) != NULL) {
        if ((s = conf_find_entry("services-host", clp, 1)) != NULL)
            services_host = strdup(s);
        if ((s = conf_find_entry("stats-host", clp, 1)) != NULL)
            stats_host = strdup(s);
    }

    if (services_host == NULL)
        services_host = strdup("someone.forgot.to.configure.stuff");
    if (stats_host == NULL)
        stats_host = strdup(services_host);

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
