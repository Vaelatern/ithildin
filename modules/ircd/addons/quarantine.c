/*
 * quarantine.c: channel/nickname quarantining
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module allows for quarantined (unuseable) nicknames and channels to be
 * configured.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/quarantine.h"

IDSTRING(rcsid, "$Id: quarantine.c 751 2006-06-23 01:43:45Z wd $");

MODULE_REGISTER("$Rev: 751 $");
/*
@DEPENDENCIES@: ircd
*/

struct quarantine_module_data quarantine;

HOOK_FUNCTION(quarantine_conf_hook);
HOOK_FUNCTION(quarantine_nick_hook);
HOOK_FUNCTION(quarantine_chan_hook);
XINFO_FUNC(xinfo_quarantine_handler);

/* this function adds a quarantine.  it only takes a 'mask' parameter, and
 * determines based on the mask which type of quarantine it is, and inserts it
 * into the proper list */
quarantine_t *add_quarantine(char *mask) {
    quarantine_t *qp = find_quarantine(mask);

    if (qp != NULL)
        return qp;

    qp = malloc(sizeof(quarantine_t));
    memset(qp, 0, sizeof(quarantine_t));
    qp->mask = strdup(mask);
    qp->class = NULL;
    qp->conf = NULL;

    /* is it a channel or a nick? */
    if (check_channame(qp->mask))
        LIST_INSERT_HEAD(quarantine.channels, qp, lp);
    else
        LIST_INSERT_HEAD(quarantine.nicks, qp, lp);

    return qp;
}

/* this function returns the quarantine, if any, for the given mask.  it
 * correctly determines which list to get the quarantine from. */
quarantine_t *find_quarantine(char *mask) {
    quarantine_t *qp;

    if (check_channame(mask))
        qp = LIST_FIRST(quarantine.channels);
    else
        qp = LIST_FIRST(quarantine.nicks);

    while (qp != NULL) {
        if (!strcmp(mask, qp->mask))
            return qp;
        qp = LIST_NEXT(qp, lp);
    }

    return NULL;
}

/* this function takes care of releasing the memory for a quarantine and
 * removing it from its list. */
void remove_quarantine(quarantine_t *qp) {

    free(qp->mask);
    if (qp->class != NULL)
        free(qp->class);
    if (qp->reason != NULL)
        free(qp->reason);
    if (qp->type != NULL)
        free(qp->type);

    LIST_REMOVE(qp, lp);
    free(qp);
}

/* this function first removes all 'configured' quarantines, then reads them
 * all in from the conf, adding them as it goes. */
HOOK_FUNCTION(quarantine_conf_hook) {
    quarantine_t *qp, *qp2;
    conf_entry_t *cep;
    conf_list_t *clp;
    char *s;
    char *reason;
    char *class;

    /* first, scrub out our lists.  we only delete stuff with 'conf' pointers
     * that are non-NULL */
    qp = LIST_FIRST(quarantine.nicks);
    while (qp != NULL) {
        qp2 = LIST_NEXT(qp, lp);
        if (qp->conf != NULL)
            remove_quarantine(qp);
        qp = qp2;
    }
    qp = LIST_FIRST(quarantine.channels);
    while (qp != NULL) {
        qp2 = LIST_NEXT(qp, lp);
        if (qp->conf != NULL)
            remove_quarantine(qp);
        qp = qp2;
    }

    cep = NULL;
    while ((cep = conf_find_next("quarantine", NULL, CONF_TYPE_LIST, cep,
                    *ircd.confhead, 1)) != NULL) {
        clp = cep->list;
        reason = conf_find_entry("reason", clp, 1);
        class = conf_find_entry("class", clp, 1);
        s = NULL;
        while ((s = conf_find_entry_next("mask", s, clp, 1)) != NULL) {
            qp = add_quarantine(s);
            qp->conf = clp;
            if (reason != NULL)
                qp->reason = strdup(reason);
            if (class != NULL)
                qp->class = strdup(class);
        }
    }

    return NULL;
}

/* this function checks to see if a nick change is valid against the list of
 * quarantined nicknames. */
HOOK_FUNCTION(quarantine_nick_hook) {
    struct client_check_args *ccap = (struct client_check_args *)data;
    quarantine_t *qp;

    if (BPRIV(ccap->from, quarantine.bypass_priv))
        return (void *)HOOK_COND_OK; /* mm.  should we return ALWAYSOK? */

    LIST_FOREACH(qp, quarantine.nicks, lp) {
        if (qp->class != NULL && ccap->from->conn != NULL &&
                !match(qp->class, ccap->from->conn->cls->name))
            continue; /* class specified and doesn't match */

        if (match(qp->mask, ccap->extra)) {
            if (!CLIENT_REGISTERED(ccap->from))
                sendto_flag(SFLAG("SPY"), "unregistered client from %s trying "
                        "to use quarantined nickname %s",
                        ccap->from->conn->host, ccap->extra);
            else
                sendto_flag(SFLAG("SPY"), "%s!%s@%s trying to use quarantined "
                        "nickname %s", ccap->from->nick, ccap->from->user,
                        ccap->from->host, ccap->extra);
            /* send the error message here.. */
            sendto_one(ccap->from, RPL_FMT(ccap->from, ERR_ERRONEOUSNICKNAME),
                    ccap->extra,
                    (qp->reason != NULL ? qp->reason : "nickname unavailable"),
                    qp->mask);
            return (void *)HOOK_COND_NEVEROK;
        }
    }

    return (void *)HOOK_COND_OK;
}

/* and this function checks to see if a user is trying to join a quarantined
 * channel. */
HOOK_FUNCTION(quarantine_chan_hook) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;
    quarantine_t *qp;

    if (BPRIV(ccap->cli, quarantine.bypass_priv))
        return (void *)HOOK_COND_OK; /* same as above? */

    LIST_FOREACH(qp, quarantine.channels, lp) {
        if (qp->class != NULL && ccap->cli->conn != NULL &&
                !match(qp->class, ccap->cli->conn->cls->name))
            continue; /* class specified and doesn't match */

        if (match(qp->mask, ccap->chan->name)) {
            sendto_flag(SFLAG("SPY"), "%s!%s@%s trying to join quarantined "
                    "channel %s", ccap->cli->nick, ccap->cli->user,
                    ccap->cli->host, ccap->chan->name);
            /* send the error message here.. */
            sendto_one(ccap->cli, RPL_FMT(ccap->cli, ERR_CHANBANREASON),
                    ccap->chan->name, "join", (qp->reason != NULL ?
                        qp->reason : "nickname unavailable"));
            return (void *)HOOK_COND_NEVEROK;
        }
    }

    return (void *)HOOK_COND_OK;
}

XINFO_FUNC(xinfo_quarantine_handler) {
    char rpl[XINFO_LEN];
    quarantine_t *qp;

#define SEND_QUARANTINE_XINFO(__list) do {                                \
    LIST_FOREACH(qp, __list, lp) {                                        \
        snprintf(rpl, XINFO_LEN, "MASK %s", qp->mask);                        \
        if (qp->class != NULL) {                                        \
            strlcat(rpl, " CLASS ", XINFO_LEN);                                \
            strlcat(rpl, qp->class, XINFO_LEN);                                \
        }                                                                \
        if (qp->type != NULL) {                                                \
            strlcat(rpl, " TYPE ", XINFO_LEN);                                \
            strlcat(rpl, qp->type, XINFO_LEN);                                \
        }                                                                \
        if (qp->reason != NULL) {                                        \
            strlcat(rpl, " REASON ", XINFO_LEN);                        \
            strlcat(rpl, qp->reason, XINFO_LEN);                        \
        }                                                                \
        sendto_one(cli, RPL_FMT(cli, RPL_XINFO), "QUARANTINE", rpl);        \
    }                                                                        \
} while (0)

    SEND_QUARANTINE_XINFO(quarantine.nicks);
    SEND_QUARANTINE_XINFO(quarantine.channels);
#undef SEND_QUARANTINE_XINFO
}

MODULE_LOADER(quarantine) {
    int64_t i64 = 0;

    memset(&quarantine, 0, sizeof(quarantine));
    if (!get_module_savedata(savelist, "quarantine", &quarantine)) {
        quarantine.bypass_priv = create_privilege("bypass-quarantine",
                PRIVILEGE_FL_BOOL, &i64, NULL);
        LIST_ALLOC(quarantine.nicks);
        LIST_ALLOC(quarantine.channels);
    }

    add_xinfo_handler(xinfo_quarantine_handler, "QUARANTINE", 0, 
            "Provides a list of nickname and channel quarantines");

    add_hook(me.events.read_conf, quarantine_conf_hook);
    add_hook(ircd.events.can_join_channel, quarantine_chan_hook);
    add_hook(ircd.events.can_nick_client, quarantine_nick_hook);

    quarantine_conf_hook(NULL, NULL);

    return 1;
}
MODULE_UNLOADER(quarantine) {

    if (reload)
        add_module_savedata(savelist, "quarantine", sizeof(quarantine),
                &quarantine);
    else {
        destroy_privilege(quarantine.bypass_priv);
        while (!LIST_EMPTY(quarantine.nicks))
            remove_quarantine(LIST_FIRST(quarantine.nicks));
        while (!LIST_EMPTY(quarantine.channels))
            remove_quarantine(LIST_FIRST(quarantine.channels));
    }

    remove_xinfo_handler(xinfo_quarantine_handler);

    remove_hook(me.events.read_conf, quarantine_conf_hook);
    remove_hook(ircd.events.can_join_channel, quarantine_chan_hook);
    remove_hook(ircd.events.can_nick_client, quarantine_nick_hook);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
