/*
 * silence.c: the SILENCE command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "commands/away.h"

IDSTRING(rcsid, "$Id: silence.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
*/

#define MAXSILENCE 10 /* maximum silence entries */
static struct {
    int max_priv;
    struct mdext_item *mdext;
} silence;

struct silence {
    char *mask;
    SLIST_ENTRY(silence) lp;
};

struct client_silences {
    SLIST_HEAD(, silence) list;
    int count;
};

HOOK_FUNCTION(silence_send_hook);
HOOK_FUNCTION(silence_mdext_hook);

MODULE_LOADER(silence) {
    int64_t i64 = MAXSILENCE;

    if (!get_module_savedata(savelist, "silence", &silence)) {
        silence.max_priv = create_privilege("maxsilence", PRIVILEGE_FL_INT,
                &i64, NULL);
        silence.mdext = create_mdext_item(ircd.mdext.client,
                sizeof(struct client_silences));
    }
    add_isupport("SILENCE", ISUPPORT_FL_PRIV, (char *)&silence.max_priv);

    add_hook(ircd.events.can_send_client, silence_send_hook);
    add_hook(ircd.mdext.client->destroy, silence_mdext_hook);

#define RPL_SILELIST 271
    CMSG("271", "%s %s");
#define RPL_ENDOFSILELIST 272
    CMSG("272", ":End of /SILENCE list.");
#define ERR_SILELISTFULL 511
    CMSG("511", "%s :Your silence list is full (%d entries)");

    return 1;
}
MODULE_UNLOADER(silence) {

    if (reload)
        add_module_savedata(savelist, "silence", sizeof(silence), &silence);
    else {
        destroy_privilege(silence.max_priv);
        destroy_mdext_item(ircd.mdext.client, silence.mdext);
    }

    del_isupport(find_isupport("SILENCE"));

    remove_hook(ircd.events.can_send_client, silence_send_hook);
    remove_hook(ircd.mdext.client->destroy, silence_mdext_hook);

    DMSG(RPL_SILELIST);
    DMSG(RPL_ENDOFSILELIST);
    DMSG(ERR_SILELISTFULL);
}

/* the silence command.  traditionally the behavior was to distribute the
 * client list across the network.  I've removed that behavior (for now).
 * Also, it was possible to see the silence list for other users.  That is also
 * no longer possible. */
CLIENT_COMMAND(silence, 0, 1, 0) {
    char *mask;
    struct client_silences *csp =
        (struct client_silences *)mdext(cli, silence.mdext);
    struct silence *sp;

    if (!MYCLIENT(cli))
        return COMMAND_WEIGHT_NONE; /* ugh.. */
    if (argc < 2 || *argv[1] == '\0' || (find_client(argv[1]) == cli)) {
        SLIST_FOREACH(sp, &csp->list, lp)
            sendto_one(cli, RPL_FMT(cli, RPL_SILELIST), cli->nick, sp->mask);
        sendto_one(cli, RPL_FMT(cli, RPL_ENDOFSILELIST));
        
        return COMMAND_WEIGHT_MEDIUM;
    }
    mask = argv[1];
    if (*mask == '+') {
        /* adding an entry .. */
        char *tmask;
        mask++;
        if (csp->count >= IPRIV(cli, silence.max_priv)) {
            sendto_one(cli, RPL_FMT(cli, ERR_SILELISTFULL), mask,
                    IPRIV(cli, silence.max_priv));
            return COMMAND_WEIGHT_MEDIUM;
        }

        tmask = make_client_mask(mask);
        if (strlen(tmask) > NICKLEN + USERLEN + HOSTLEN + 3)
            tmask[NICKLEN + USERLEN + HOSTLEN + 2] = '\0';

        SLIST_FOREACH(sp, &csp->list, lp) {
            if (!strcasecmp(tmask, sp->mask))
                break; /* got it */
        }
        if (sp == NULL) {
            sp = malloc(sizeof(struct silence));
            sp->mask = strdup(tmask);
            SLIST_INSERT_HEAD(&csp->list, sp, lp);
            csp->count++;
            sendto_one_target(cli, cli, NULL, NULL, "SILENCE", "+%s",
                    sp->mask);
        }
    } else if (*mask == '-') {
        /* removing an entry.. */
        char *s;

        mask++;
        s = make_client_mask(mask);
        SLIST_FOREACH(sp, &csp->list, lp) {
            if (!strcasecmp(s, sp->mask))
                break; /* got it */
        }
        if (sp != NULL) {
            sendto_one_target(cli, cli, NULL, NULL, "SILENCE", "-%s",
                    sp->mask);
            SLIST_REMOVE(&csp->list, sp, silence, lp);
            free(sp->mask);
            free(sp);
            csp->count--;
        }
    }

    return COMMAND_WEIGHT_MEDIUM;
}

HOOK_FUNCTION(silence_send_hook) {
    struct client_check_args *ccap = (struct client_check_args *)data;
    struct client_silences *csp =
        (struct client_silences *)mdext(ccap->to, silence.mdext);
    struct silence *sp;
    char mask[NICKLEN + USERLEN + HOSTLEN + 3];

    if (csp->count == 0)
        return (void *)HOOK_COND_NEUTRAL; /* don't bother */

    sprintf(mask, "%s!%s@%s", ccap->from->nick, ccap->from->user,
            ccap->from->host);
    SLIST_FOREACH(sp, &csp->list, lp) {
        if (match(sp->mask, mask))
            return (void *)HOOK_COND_NOTOK; /* silenced.. */
    }
    return (void *)HOOK_COND_NEUTRAL; /* no match */
}

HOOK_FUNCTION(silence_mdext_hook) {
    struct client_silences *csp =
        (struct client_silences *)mdext_offset(data, silence.mdext);
    struct silence *sp;

    while ((sp = SLIST_FIRST(&csp->list)) != NULL) {
        SLIST_REMOVE_HEAD(&csp->list, lp);
        free(sp->mask);
        free(sp);
    }

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
