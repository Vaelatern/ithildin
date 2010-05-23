/*
 * hostmask.c: configuration based user-hostmasking
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module adds a 'hostmask' entry to the class configuration section,
 * which allows users connecting in a specific class to mask their hostnames
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: hostmask.c 820 2008-09-30 04:34:10Z wd $");

MODULE_REGISTER("$Rev: 820 $");
/*
@DEPENDENCIES@: ircd
*/

#define CGI_IRC_SPECIAL_MASK "cgi:irc"

static struct mdext_item *class_hostmask_mdi;

HOOK_FUNCTION(hostmask_conf_hook);
HOOK_FUNCTION(hostmask_cc_hook);
static int validate_cgiirc_host(const char *, char **, char **);

MODULE_LOADER(hostmask) {

    class_hostmask_mdi = create_mdext_item(ircd.mdext.class, HOSTLEN + 1);
    /* since we depend on ircd, it gets first shot at the conf hook, which
     * means it will parse its conf well before we get to.  whew. :) */
    add_hook(me.events.read_conf, hostmask_conf_hook);
    add_hook(ircd.events.client_connect, hostmask_cc_hook);
    
    hostmask_conf_hook(NULL, NULL);

    return 1;
}
MODULE_UNLOADER(hostmask) {

    destroy_mdext_item(ircd.mdext.class, class_hostmask_mdi);
    remove_hook(me.events.read_conf, hostmask_conf_hook);
    remove_hook(ircd.events.client_connect, hostmask_cc_hook);
}

HOOK_FUNCTION(hostmask_conf_hook) {
    class_t *cls;
    char *s;

    LIST_FOREACH(cls, ircd.lists.classes, lp) {
        *(mdext(cls, class_hostmask_mdi)) = '\0';
        if ((s = conf_find_entry("hostmask", cls->conf, 1)) != NULL) {
            /* verify the mask.  make sure it consists ONLY of valid
             * characters, and that it has multiple parts. */
            if (strcasecmp(s, CGI_IRC_SPECIAL_MASK) &&
                    (!istr_okay(ircd.maps.host, s) ||
                     strchr(s, '.') == NULL))
                log_error("hostmask for class %s is invalid", cls->name);
            else {
                strlcpy(mdext(cls, class_hostmask_mdi), s, HOSTLEN + 1);
                log_debug("setting hostmask for class %s to %s", cls->name,
                        mdext(cls, class_hostmask_mdi));
            }
        }
    }
    
    return NULL;
}

HOOK_FUNCTION(hostmask_cc_hook) {
    client_t *cli = (client_t *)data;
    char *mask, *ip;

    if (cli->conn == NULL)
        return NULL; /* nothing to do */

    mask = ip = NULL;

    mask = mdext(cli->conn->cls, class_hostmask_mdi);
    if (mask != NULL && *mask != '\0') {
        if (!strcasecmp(mask, CGI_IRC_SPECIAL_MASK)) {
            /* Special CGI:IRC hack.  We take the password, if supplied, and
             * use it as a hostname (as long as it's valid).  Good stuff. */
            if (cli->conn->pass != NULL && *cli->conn->pass != '\0' &&
                    validate_cgiirc_host(cli->conn->pass, &ip, &mask)) {
                log_debug("cgi:irc connection masked as %s / %s", ip, mask);
                strcpy(cli->ip, ip); /* set the IP as well */
            } else {
                log_warn("cgi:irc hostmasked connection provided invalid "
                        "host (%s) (%s!%s@%s)",
                        (cli->conn->pass == NULL ? "" : cli->conn->pass),
                        cli->nick, cli->user, cli->host);
                return NULL;
            }
            /* fallthrough to normal mask case. */
        }
        sendto_flag(SFLAG("SPY"), "Changing hostname for %s!%s@%s to %s",
                cli->nick, cli->user, cli->host, mask);
        strlcpy(cli->host, mask, HOSTLEN + 1);
    }

    return NULL;
}

/* CGIIRC(6)+__(2)+\0(1)+ip+host */
#define CGIIRC_HOST_SIZE 9 + IPADDR_MAXLEN + HOSTLEN

/* validate the string from a CGI:IRC connection.  The string is of the form
 * CGIIRC_<ip>_<host>.  We ensure that both the IP and hostname are valid.  */
static int validate_cgiirc_host(const char *in, char **ip, char **host) {
    static char str[CGIIRC_HOST_SIZE];
    char *s_ip, *s_host;

    *ip = *host = NULL;
    strlcpy(str, in, CGIIRC_HOST_SIZE);
    
    if (strncmp(str, "CGIIRC_", 7))
        return 0;

    s_ip = str + 7;
    if ((s_host = strchr(s_ip, '_')) == NULL)
        return 0; /* no hostname */
    *s_host++ = '\0';

    /* now we have ip/host, let's validate */
    if (*s_ip == '\0' || *s_host == '\0')
        return 0;

    if (get_address_type(s_ip) == PF_UNSPEC)
        return 0; /* bad IP */

    if (strchr(s_host, '.') == NULL || !istr_okay(ircd.maps.host, s_host))
        return 0; /* bogus host */

    *ip = s_ip;
    *host = s_host;
    return 1;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
