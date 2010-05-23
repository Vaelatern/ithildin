/*
 * acl.c: the ACL command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/acl.h"

IDSTRING(rcsid, "$Id: acl.c 830 2009-01-25 23:08:01Z wd $");

MODULE_REGISTER("$Rev: 830 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/acl
*/

static const char *acl_kline_type = "kline";
static const char *acl_zline_type = "zline";
static const char *acl_acl_type = "acl-runtime";

MODULE_LOADER(acl) {

    /* just add aliases */
    add_command_alias("acl", "kline");
    add_command_alias("acl", "zline");
    add_command_alias("acl", "unkline");
    add_command_alias("acl", "unzline");

    return 1;
}

/* this command may be called in a variety of forms to perform a variety of
 * actions.  as the ACL command it has a syntax like:
 * ACL [add|del] [stage <stg>] [mask <mask>] [access <access> [reason
 * <reason>] [expire <expire>].  This is mostly useful as a way to add bans,
 * but could be used to add temporary access entries (not usually a good idea,
 * as they circumvent all previously added bans).  The command may also be
 * invoked as one of: KLINE, UNKLINE, ZLINE, UNZLINE and will act as those
 * commands did in older daemons.  When listing, not all the specifiers are
 * necessary or even useful (specifically expire/reason), and if others are not
 * listed it is assumed that any value is suitable. */
CLIENT_COMMAND(acl, 0, 0, COMMAND_FL_OPERATOR) {
    acl_t *ap = NULL;
    time_t expire = 0;
    int oarg = 1;
    const char *type = acl_acl_type;
    char mask[ACL_USERLEN + ACL_HOSTLEN + 2];
    int stage = -1;
    int acc = -1;
    char reason[XINFO_LEN];
#define ACMD_NONE 0
#define ACMD_ADD 1
#define ACMD_DEL 2
    int act = ACMD_NONE;

    *mask = '\0';
    strcpy(reason, "no reason given");
    /* see if we were invoked as some other command */
    if (!strcasecmp(argv[0], "KLINE")) {
        if (argc > oarg && isdigit(*argv[oarg]))
            expire = str_conv_time(argv[oarg++], 1800);
        else
            expire = 1800; /* 30 minutes by default */
        if (argc <= oarg || (strchr(argv[oarg], '!') != NULL ||
                strchr(argv[oarg], '@') == NULL)) {
            sendto_one(cli, "NOTICE",
                    ":Usage: KLINE [time] <user@host-mask> [:reason]");
            return COMMAND_WEIGHT_NONE;
        }
        
        act = ACMD_ADD;
        type = acl_kline_type;
        stage = ACL_STAGE_REGISTER;
        acc = ACL_DENY;
        strlcpy(mask, argv[oarg++], ACL_USERLEN + ACL_HOSTLEN + 2);
        if (argc > oarg)
            strlcpy(reason, argv[oarg], XINFO_LEN);
    } else if (!strcasecmp(argv[0], "UNKLINE")) {
        if (argc <= oarg || (strchr(argv[oarg], '!') != NULL ||
                strchr(argv[oarg], '@') == NULL)) {
            sendto_one(cli, "NOTICE",
                    ":Usage: UNKLINE <user@host-mask>");
            return COMMAND_WEIGHT_NONE;
        }

        act = ACMD_DEL;
        type = acl_kline_type;
        stage = ACL_STAGE_REGISTER;
        acc = ACL_DENY;
        strlcpy(mask, argv[oarg++], ACL_USERLEN + ACL_HOSTLEN + 2);
    } else if (!strcasecmp(argv[0], "ZLINE")) {
        if (argc <= oarg || (strchr(argv[oarg], '!') != NULL ||
                strchr(argv[oarg], '@') != NULL)) {
            sendto_one(cli, "NOTICE",
                    ":Usage: ZLINE <ip-mask> [:reason]");
            return COMMAND_WEIGHT_NONE;
        }
        
        act = ACMD_ADD;
        type = acl_zline_type;
        stage = ACL_STAGE_CONNECT;
        acc = ACL_DENY;
        strlcpy(mask, argv[oarg++], ACL_USERLEN + ACL_HOSTLEN + 2);
        if (argc > oarg)
            strlcpy(reason, argv[oarg], XINFO_LEN);
    } else if (!strcasecmp(argv[0], "UNZLINE")) {
        if (argc <= oarg || (strchr(argv[oarg], '!') != NULL ||
                strchr(argv[oarg], '@') != NULL)) {
            sendto_one(cli, "NOTICE",
                    ":Usage: UNZLINE <ip-mask>");
            return COMMAND_WEIGHT_NONE;
        }

        act = ACMD_DEL;
        type = acl_zline_type;
        stage = ACL_STAGE_CONNECT;
        acc = ACL_DENY;
        strlcpy(mask, argv[oarg++], ACL_USERLEN + ACL_HOSTLEN + 2);
    } else {
        /* treat it like it was a real ACL command. */
        if (argc > oarg) {
            if (!strcasecmp(argv[oarg], "ADD")) {
                act = ACMD_ADD;
                /* set some defaults for them */
                stage = 3;
                acc = ACL_DENY;
            } else if (!strcasecmp(argv[oarg], "DEL")) {
                act = ACMD_DEL;
                /* set some defaults */
                stage = 3;
                acc = ACL_DENY;
            } else {
                sendto_one(cli, "NOTICE",
                        "unknown ACL command %s", argv[oarg]);
                return COMMAND_WEIGHT_NONE;
            }
            oarg++;
        }

        while (argc > oarg) {
            if (!strcasecmp(argv[oarg], "STAGE")) {
                oarg++;
                if (argc > oarg) {
                    stage = str_conv_int(argv[oarg], 0);
                    if (stage < ACL_STAGE_CONNECT || stage >
                            ACL_STAGE_REGISTER) {
                        sendto_one(cli, "NOTICE",
                                ":Invalid stage %s", argv[oarg]);
                        return COMMAND_WEIGHT_NONE;
                    }
                    oarg++;
                } else {
                    sendto_one(cli, "NOTICE",
                            ":Missing argument to STAGE specifier");
                    return COMMAND_WEIGHT_NONE;
                }
            } else if (!strcasecmp(argv[oarg], "MASK")) {
                oarg++;
                if (argc > oarg)
                    strlcpy(mask, argv[oarg++], ACL_USERLEN + ACL_HOSTLEN + 2);
                else {
                    sendto_one(cli, "NOTICE",
                            ":Missing argument to MASK specifier");
                    return COMMAND_WEIGHT_NONE;
                }
            } else if (!strcasecmp(argv[oarg], "ACCESS")) {
                oarg++;
                if (argc > oarg) {
                    if (!strcasecmp(argv[oarg], "DENY"))
                        acc = ACL_DENY;
                    else if (!strcasecmp(argv[oarg], "ALLOW"))
                        acc = ACL_ALLOW;
                    else {
                        sendto_one(cli, "NOTICE",
                                ":Invalid ACCESS specifier %s", argv[oarg]);
                        return COMMAND_WEIGHT_NONE;
                    }
                    oarg++;
                } else {
                    sendto_one(cli, "NOTICE",
                            ":Missing argument to ACCESS specifier");
                    return COMMAND_WEIGHT_NONE;
                }
            } else if (!strcasecmp(argv[oarg], "REASON")) {
                oarg++;
                if (argc > oarg)
                    strlcpy(reason, argv[oarg++], XINFO_LEN);
                else {
                    sendto_one(cli, "NOTICE",
                            ":Missing argument to REASON specifier");
                    return COMMAND_WEIGHT_NONE;
                }
            } else if (!strcasecmp(argv[oarg], "EXPIRE")) {
                oarg++;
                if (argc > oarg)
                    expire = str_conv_time(argv[oarg++], 0);
                else {
                    sendto_one(cli, "NOTICE",
                            ":Missing argument to EXPIRE specifier");
                    return COMMAND_WEIGHT_NONE;
                }
            } else if (!strcasecmp(argv[oarg], "TYPE")) {
                oarg++;
                if (argc > oarg)
                    type = argv[oarg];
                else {
                    sendto_one(cli, "NOTICE",
                            ":Missing argument to TYPE specifier");
                    return COMMAND_WEIGHT_NONE;
                }
            } else {
                sendto_one(cli, "NOTICE",
                        ":Unknown specifier %s", argv[oarg++]);
                return COMMAND_WEIGHT_NONE;
            }
        }
    }

    /* see if they passed useful stuff */
    if (act == ACMD_NONE || *mask == '\0' || stage == -1 || acc == -1) {
        sendto_one(cli, RPL_FMT(cli, ERR_NEEDMOREPARAMS), argv[0]);
        return COMMAND_WEIGHT_NONE;
    }

    /* see if it exists */
    ap = find_acl(stage, acc, mask, type, ACL_DEFAULT_RULE, NULL, NULL);

    /* are we adding..? */
    if (act == ACMD_ADD) {
        if (ap != NULL) {
            sendto_one(cli, "NOTICE",
                    "There is already an ACL in place for %s", mask);
            return COMMAND_WEIGHT_NONE;
        }

        ap = create_acl(stage, acc, mask, type, ACL_DEFAULT_RULE);
        ap->reason = strdup(reason);
        /* if the expire time is non-zero, set a conf to 0x1 so it will get
         * nuked by a rehash too */
        if (expire) {
            ap->conf = ACL_CONF_TEMP;
            acl_add_timer(ap, expire); /* also create the timer for it */
        }

        /* don't forget to run a check in the appropriate stage to knock off
         * clients as need-be */
        acl_force_check(ap->stage, ap, cli->nick, true);
    } else if (act == ACMD_DEL) {
        if (ap == NULL) {
            sendto_one(cli, "NOTICE", "There is no ACL in place for %s", mask);
            return COMMAND_WEIGHT_NONE;
        }

        if (ap->conf != NULL && ap->conf != ACL_CONF_TEMP) {
            sendto_one(cli, "NOTICE", "You cannot remove configured ACLs");
            return COMMAND_WEIGHT_NONE;
        }

        sendto_flag(ircd.sflag.ops,
                "%s removed %s for %s (stage %d, access %s)",
                cli->nick, ap->type, mask, stage,
                (ap->access == ACL_DENY ? "DENY" : (ap->access == ACL_ALLOW ?
                                                    "ALLOW" : "UNKNOWN")));
        destroy_acl(ap);
    }

    return COMMAND_WEIGHT_NONE; /* and nothing else matters! */
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
