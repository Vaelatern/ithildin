/*
 * services.c: the entry point for the services module
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains the module load/unload functions, and some other
 * 'generic' stuff that isn't found elsewhere
 */

#include <ithildin/stand.h>

#include "services.h"

IDSTRING(rcsid, "$Id: services.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("0.1");
/*
@DEPENDENCIES@: ircd
*/

struct services_struct services;
static struct {
    uint64_t sync;
    uint64_t mail;
} timers;

static HOOK_FUNCTION(services_privmsg_hook);
static HOOK_FUNCTION(services_timer_hook);

static HOOK_FUNCTION(services_privmsg_hook) {
    struct command_hook_args *args = (struct command_hook_args *)data;
    client_t *cli = args->cli;
    int argc = 0;
    char *argv[20];
    char *s;

    if (args->cli == NULL)
        return NULL; /* notices/messages from servers are of no interest */

    if (strcmp(args->argv[0], "PRIVMSG"))
        return NULL; /* not a privmsg. */

    /* break up the command arguments.. */
    s = args->argv[2];
    while ((argv[argc] = strsep(&s, " \t")) != NULL) {
        if (*argv[argc] != '\0')
            argc++;
        if (argc == 20)
            break;
    }

    /* find the service which we're after.. */
    if (!nickcmp(services.nick.client->nick, args->argv[1], NICKLEN))
        nick_handle_msg(cli, argc, argv);
    else if (!nickcmp(services.oper.client->nick, args->argv[1], NICKLEN))
        oper_handle_msg(cli, argc, argv);

    return NULL;
}

static HOOK_FUNCTION(services_timer_hook) {
    uint64_t *ref = (uint64_t *)data;

    if (ref == &timers.sync)
        db_sync();
    else if (ref == &timers.mail)
        mail_send();

    return NULL;
}

MODULE_LOADER(services) {
    module_t *mp = find_module("services");

    memset(&services, 0, sizeof(services));
    services.confhead = confdata;

    /* overwrite the version string... */
    snprintf(ircd.version, GCOSLEN, "%s+services%s", me.version,
            mp->header->version);
    snprintf(ircd.vercomment, TOPICLEN,
            "services killed the eggdrop bot star");

    get_module_savedata(savelist, "services.db", &services.db);

    if (!get_module_savedata(savelist, "services.expires",
                &services.expires)) {
        services.expires.nick = 86400 * 30; /* 30 days */
        services.expires.chan = 86400 * 30;
        services.expires.memo = 86400 * 15;
    }

    if (!get_module_savedata(savelist, "services.mail", &services.mail)) {
        strcpy(services.mail.host, "127.0.0.1");
        strcpy(services.mail.port, "25");
        strcpy(services.mail.from, "services@localhost");
        services.mail.interval = 300;
        services.mail.templates.activate = strdup("No template provided! :(");
    }
    
    if (!get_module_savedata(savelist, "services.limits", &services.limits)) {
        services.limits.mail_sends = 48 * 3600;
        services.limits.mail_nicks = 10;
        services.limits.linked_nicks = 5;
        services.limits.nick_acclist = 10;
    }

    if (!get_module_savedata(savelist, "services.nick", &services.nick))
        services.nick.type = NICK_SERVICE;
    if (!get_module_savedata(savelist, "services.oper", &services.oper))
        services.oper.type = OPERATOR_SERVICE;

    /* add some mdext items to ircd structures */
    services.mdext.client = create_mdext_item(ircd.mdext.client,
            sizeof(struct services_client_data));

    /* and hook onto whatever stuff we want to .. */
    command_add_hook("PRIVMSG", 1, services_privmsg_hook, 0);
    add_hook(ircd.events.register_client, nick_register_hook);
    add_hook(ircd.events.unregister_client, nick_register_hook);

    if (!reload && !services_parse_conf(*services.confhead))
        return 0;

    /* add in the timers.. */
    timers.sync = create_timer(-1, 1800, services_timer_hook,
            (char *)&timers.sync);
    timers.mail = create_timer(-1, services.mail.interval, services_timer_hook,
            (char *)&timers.mail);

    /* now call some setup functions... */
    if (!reload) {
        nick_setup();
        oper_setup();
        mail_setup();
    }

    /* and we need to start the database engine.. */
    if (!reload && !db_start())
        return 0;

    return 1;
}

MODULE_UNLOADER(services) {

    /* make sure to do any pending database cleanup.. */
    db_cleanup();

    destroy_mdext_item(ircd.mdext.client, services.mdext.client);
    command_remove_hook("PRIVMSG", 1, services_privmsg_hook);

    if (reload) {
        add_module_savedata(savelist, "services.db", sizeof(services.db),
                &services.db);
        add_module_savedata(savelist, "services.expires",
                sizeof(services.expires), &services.expires);
        add_module_savedata(savelist, "services.mail", sizeof(services.mail),
                &services.mail);
        add_module_savedata(savelist, "services.limits",
                sizeof(services.limits), &services.limits);
        add_module_savedata(savelist, "services.nick",
                sizeof(services.nick), &services.nick);
        add_module_savedata(savelist, "services.oper",
                sizeof(services.oper), &services.oper);
    }
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
