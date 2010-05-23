/*
 * akill.c: the AKILL (and some others ;) command.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/acl.h"

IDSTRING(rcsid, "$Id: akill.c 830 2009-01-25 23:08:01Z wd $");

MODULE_REGISTER("$Rev: 830 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/acl
*/

static const char *acl_akill_type = "akill";
static const char *acl_sgline_type = "sgline";
static const char *acl_szline_type = "szline";

HOOK_FUNCTION(akill_se_hook);

MODULE_LOADER(akill) {

    add_command_alias("akill", "rakill");
    add_command_alias("akill", "szline");
    add_command_alias("akill", "unszline");
    add_command_alias("akill", "sgline");
    add_command_alias("akill", "unsgline");
    add_hook(ircd.events.server_establish, akill_se_hook);

    return 1;
}
MODULE_UNLOADER(akill) {

    remove_hook(ircd.events.server_establish, akill_se_hook);
}

/* This command may be called with a variety of names.  It is intended to
 * function for setting any ACLs necessary from a master server. */
SERVER_COMMAND(akill, 0, 0, 0) {
    acl_t *ap = NULL;
    char mask[ACL_USERLEN + ACL_HOSTLEN + 2];
#define ACL_ADD 0
#define ACL_DEL 1
    int op = ACL_ADD;
    int stage = ACL_STAGE_REGISTER;
    time_t expire = 0;
    char *reason = NULL;
    const char *type = acl_akill_type;
    char *info = NULL;

    /* XXX: This is a hack to allow sglinebursts to be sent from non-master
     * servers.  I guess we should detect the bursting condition and only
     * accept these at that time, but that's not we do it. :) */
    if (!SERVER_MASTER(srv) && strcasecmp(argv[0], "SGLINE") &&
            strcasecmp(argv[0], "UNSGLINE")) {
        sendto_serv_butone(NULL, NULL, ircd.me, NULL, "GLOBOPS",
                ":Non-master server %s trying to %s", srv->name, argv[0]);
        sendto_flag(SFLAG("GLOBOPS"), "Non-master server %s trying to %s",
                srv->name, argv[0]);
        return 0; /* no access. */
    }

    if (!strcasecmp(argv[0], "AKILL")) {
        char *user, *host, *set_by;
        time_t set_at;
        /* arguments: host user length set-by set-at reason */
        if ((!SERVER_SUPPORTS(sptr, PROTOCOL_SFL_SHORTAKILL) && argc != 7) ||
                (SERVER_SUPPORTS(sptr, PROTOCOL_SFL_SHORTAKILL) && argc < 3)) {
            sendto_flag(ircd.sflag.ops, "bogus akill command from %s",
                    srv->name);
            return 0;
        }

        host = argv[1];
        user = argv[2];
        if (SERVER_SUPPORTS(sptr, PROTOCOL_SFL_SHORTAKILL)) {
            reason = (argc > 3 ? argv[3] : "<no reason>");

            /* others we don't get ... */
            set_by = "<unknown>";
            set_at = me.now;
            expire = 0;
        } else {
            expire = str_conv_time(argv[3], 0);
            set_by = argv[4];
            set_at = str_conv_time(argv[5], me.now);
            reason = argv[6];
        }
        snprintf(mask, ACL_USERLEN + ACL_HOSTLEN + 2, "%s@%s", user, host);

        /* propogate out.  If we're getting data from a 'SHORTAKILL' server
         * we'll end up sending out some fallacious/made up stuff.  Oh well! */
        sendto_serv_pflag_butone(PROTOCOL_SFL_SHORTAKILL, true, sptr, NULL,
                srv, NULL, "AKILL", "%s %s :%s", host, user, reason);
        sendto_serv_pflag_butone(PROTOCOL_SFL_SHORTAKILL, false, sptr, NULL,
                srv, NULL, "AKILL", "%s %s %d %s %d :%s", host, user,
                expire, set_by, set_at, reason);
    } else if (!strcasecmp(argv[0], "RAKILL")) {
        /* arguments: host user */

        if (argc < 3) {
            sendto_flag(ircd.sflag.ops, "bogus rakill command from %s",
                    srv->name);
            return 0;
        }

        snprintf(mask, ACL_USERLEN + ACL_HOSTLEN + 2, "%s@%s", argv[2], argv[1]);
        op = ACL_DEL;
        sendto_serv_butone(sptr, NULL, srv, NULL, "RAKILL", "%s %s", argv[1],
                argv[2]);
    } else if (!strcasecmp(argv[0], "SZLINE")) {
        /* arguments: host [reason] */
        if (argc < 2) {
            sendto_flag(ircd.sflag.ops, "bogus szline command from %s",
                    srv->name);
            return 0;
        }

        stage = ACL_STAGE_CONNECT;
        type = acl_szline_type;
        strlcpy(mask, argv[1], ACL_HOSTLEN + 1);
        if (argc > 2)
            reason = argv[2];
        else
            reason = "No Reason";

        sendto_serv_butone(sptr, NULL, srv, NULL, "SZLINE", "%s :%s", argv[1],
                reason);
    } else if (!strcasecmp(argv[0], "UNSZLINE")) {
        /* arguments: host */
        if (argc != 2) {
            sendto_flag(ircd.sflag.ops, "bogus unszline command from %s",
                    srv->name);
            return 0;
        }

        stage = ACL_STAGE_CONNECT;
        type = acl_szline_type;
        strlcpy(mask, argv[1], ACL_HOSTLEN + 1);

        sendto_serv_butone(sptr, NULL, srv, NULL, "UNSZLINE", "%s", argv[1]);
    } else if (!strcasecmp(argv[0], "SGLINE")) {
        /* arguments: length mask[:reason] */
        int len = 0;
        if (argc != 3) {
            sendto_flag(ircd.sflag.ops, "bogus sgline command from %s",
                    srv->name);
            return 0;
        }

        type = acl_sgline_type;
        strcpy(mask, "*");
        info = argv[1];
        len = str_conv_int(argv[1], 0);
        if (strlen(info) > len && info[len] == ':') {
            info[len] = '\0';
            reason = info + len + 1;
        } else
            reason = "No Reason";

        sendto_serv_butone(sptr, NULL, srv, NULL, "SGLINE", "%s :%s", argv[1],
                argv[2]);
    } else if (!strcasecmp(argv[0], "UNSGLINE")) {
        /* arguments: host */
        acl_t *ap2;
        if (argc != 2) {
            sendto_flag(ircd.sflag.ops, "bogus unsgline command from %s",
                    srv->name);
            return 0;
        }

        sendto_serv_butone(sptr, NULL, srv, NULL, "UNSGLINE", "%s", argv[1]);
        /* unlike the others, for some reason UNSGLINE uses match() to look for
         * its victims.  As such, we check the acl list ourself.  How.. um..
         * silly. */
        ap = LIST_FIRST(acl.list);
        while (ap != NULL) {
            ap2 = LIST_NEXT(ap, lp);
            if (ap->info != NULL && match(argv[1], ap->info))
                destroy_acl(ap);
            ap = ap2;
        }
        return 0;
    }

    if (op == ACL_ADD) {
        ap = create_acl(stage, ACL_DENY, mask, type, ACL_DEFAULT_RULE);
        ap->conf = ACL_CONF_TEMP;
        if (expire)
            acl_add_timer(ap, expire);
        if (info != NULL)
            ap->info = strdup(info);
        if (reason != NULL)
            ap->reason = strdup(reason);

        acl_force_check(ap->stage, ap, srv->name, false);
    } else if (op == ACL_DEL) {
        if ((ap = find_acl(stage, ACL_DENY, mask, type, ACL_DEFAULT_RULE, NULL, info)))
            destroy_acl(ap);
    }

    return 0;
}

/* We (currently) send SZLINEs and SGLINEs down.  We do not send AKILLs, but
 * maybe we should?  Eh. */
HOOK_FUNCTION(akill_se_hook) {
    server_t *srv = (server_t *)data;
    acl_t *ap;

    /* walk the acl list, looking for ACLs which are SZLINEs or SGLINEs, and
     * send them along. */
    LIST_FOREACH(ap, acl.list, lp) {
        if (!strcmp(ap->type, acl_szline_type))
            sendto_serv(srv, "SZLINE", "%s :%s", ap->host, ap->reason);
        else if(!strcmp(ap->type, acl_sgline_type))
            sendto_serv(srv, "SGLINE", "%d :%s:%s", strlen(ap->info), ap->info,
                    ap->reason);
    }

    return NULL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
