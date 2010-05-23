/*
 * conf.c: configuration data parser
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains all the legwork functions to parse services'
 * configuration data.
 */

#include <ithildin/stand.h>

#include "services.h"

IDSTRING(rcsid, "$Id: conf.c 579 2005-08-21 06:38:18Z wd $");

/* This function handles reading the entire conf.  It might be better to break
 * it out into smaller functions later.  Not sure. */
int services_parse_conf(conf_list_t *conf) {
    service_t *sp;
    conf_entry_t *cep;
    conf_list_t *clp;
    char *s;

    cep = NULL;
    while ((cep = conf_find_next("service", NULL, CONF_TYPE_LIST, cep,
                    conf, 1)) != NULL) {
        if (cep->string == NULL) {
            log_warn("service section without name.  silly.");
            continue;
        }

        if (!strcasecmp(cep->string, "nickname"))
            sp = &services.nick;
        else if (!strcasecmp(cep->string, "operator"))
            sp = &services.oper;
        else {
            log_warn("service section with unrecognized name %s", cep->string);
            continue;
        }
        sp->conf = cep->list;

        if (sp->client == NULL)
            register_service(sp);
    }

    if ((s = conf_find_entry("database", conf, 1)) == NULL) {
        log_error("no database definition in the configuration!");
        return 0;
    }
    strlcpy(services.db.file, s, PATH_MAX);
    if (access(services.db.file, R_OK | W_OK)) {
        if (errno == ENOENT)
            log_warn("database %s does not currently exist",
                    services.db.file);
        else {
            log_error("cannot access %s: %s", services.db.file,
                    strerror(errno));
            return 0;
        }
    }

    if ((clp = conf_find_list("expiration", conf, 1)) != NULL) {
        services.expires.nick = str_conv_time(conf_find_entry("nick", clp, 1),
                86400 * 30);
        services.expires.chan = str_conv_time(conf_find_entry("chan", clp, 1),
                86400 * 30);
        services.expires.memo = str_conv_time(conf_find_entry("memo", clp, 1),
                86400 * 15);
    }

    if ((clp = conf_find_list("mail", conf, 1)) != NULL) {
        if ((s = conf_find_entry("activation-template", clp, 1)) != NULL)
            services.mail.templates.activate = strdup(s);
        if ((s = conf_find_entry("server", clp, 1)) != NULL)
            strlcpy(services.mail.host, s, FQDN_MAXLEN + 1);
        if ((s = conf_find_entry("port", clp, 1)) != NULL)
            strlcpy(services.mail.port, s, NI_MAXSERV);
        if ((s = conf_find_entry("send-from", clp, 1)) != NULL)
            strlcpy(services.mail.from, s, HOSTLEN * 2);
    }

    if ((clp = conf_find_list("limits", conf, 1)) != NULL) {
        services.limits.mail_sends = str_conv_time(
                conf_find_entry("mail", clp, 1), 48 * 3600);
        services.limits.mail_nicks = str_conv_int(
                conf_find_entry("nicks-per-address", clp, 1), 10);
        services.limits.linked_nicks = str_conv_int(
                conf_find_entry("linked-nicks", clp, 1), 5);
        services.limits.nick_acclist = str_conv_int(
                conf_find_entry("nick-access-list", clp, 1), 10);
    }

    return 1;
}

/* This is here because it broke off the conf parser up above.  Basically it
 * just registers (signs on) a service based on its configuration parameters.
 * This shouldn't be called on currently registered services! */
void register_service(service_t *sp) {
    char *s;

    sp->client = create_client(NULL);
    if ((s = conf_find_entry("name", sp->conf, 1)) != NULL)
        strlcpy(sp->client->nick, s, ircd.limits.nicklen + 1);
    else {
        switch (sp->type) {
            case NICK_SERVICE:
                strcpy(sp->client->nick, "NickServ");
                break;
            case CHANNEL_SERVICE:
                strcpy(sp->client->nick, "ChanServ");
                break;
            case MEMO_SERVICE:
                strcpy(sp->client->nick, "MemoServ");
                break;
            case OPERATOR_SERVICE:
                strcpy(sp->client->nick, "OperServ");
                break;
        }
    }
    if ((s = conf_find_entry("user", sp->conf, 1)) != NULL)
        strlcpy(sp->client->user, s, USERLEN + 1);
    else
        strcpy(sp->client->user, "service");
    if ((s = conf_find_entry("host", sp->conf, 1)) != NULL)
        strlcpy(sp->client->host, s, HOSTLEN + 1);
    else
        strcpy(sp->client->host, ircd.me->name);
    if ((s = conf_find_entry("info", sp->conf, 1)) != NULL)
        strlcpy(sp->client->info, s, GCOSLEN + 1);
    else {
        switch (sp->type) {
            case NICK_SERVICE:
                strcpy(sp->client->info, "Nickname Service");
                break;
            case CHANNEL_SERVICE:
                strcpy(sp->client->info, "Channel Service");
                break;
            case MEMO_SERVICE:
                strcpy(sp->client->info, "Memo Service");
                break;
            case OPERATOR_SERVICE:
                strcpy(sp->client->info, "Operator Service");
                break;
        }
    }
    strcpy(sp->client->ip, "127.0.0.1");
    sp->client->signon = sp->client->ts = me.now;
    sp->client->server = ircd.me;
    hash_insert(ircd.hashes.client, sp->client);
    sptr = NULL;
    register_client(sp->client);
    log_debug("registered %s!%s@%s (%s)", sp->client->nick, sp->client->user,
            sp->client->host, sp->client->info);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
