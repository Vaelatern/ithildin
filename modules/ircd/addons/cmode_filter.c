/*
 * cmode_filter.c: control-character filter mode
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module adds filtering for control characters in channel messages.  The
 * control characters filtered are configurable.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"

IDSTRING(rcsid, "$Id: cmode_filter.c 707 2006-03-27 07:46:45Z wd $");

MODULE_REGISTER("$Rev: 707 $");

/*
@DEPENDENCIES@:        ircd
@DEPENDENCIES@: ircd/addons/core
*/

static unsigned char chanmode_filter;
static char filter_chars[256];

HOOK_FUNCTION(can_send_filter);
HOOK_FUNCTION(filter_conf_hook);

MODULE_LOADER(cmode_filter) {

    if (!get_module_savedata(savelist, "chanmode_filter", &chanmode_filter))
        chanmode_request('c', &chanmode_filter, CHANMODE_FL_D,
                "chanmode_flag", "chanmode_flag_query", 0, NULL);

    add_hook_before(ircd.events.can_send_channel, can_send_filter, NULL);
    add_hook(me.events.read_conf, filter_conf_hook);

    /* XXX: I haven't included the 'Not sent: ' bit because I have yet to
     * figure out in what way it could possibly be useful, except as a way to
     * get the server to send back loads of error data instead of a simple
     * message. */
#define ERR_FILTERCHARONCHAN 408
    CMSG("408", "%s :You cannot use colors on this channel.");

    filter_conf_hook(NULL, NULL);
    return 1;
}

MODULE_UNLOADER(cmode_filter) {
    
    if (reload)
        add_module_savedata(savelist, "chanmode_filter",
                sizeof(chanmode_filter), &chanmode_filter);
    else
        chanmode_release(chanmode_filter);

    remove_hook(ircd.events.can_send_channel, can_send_filter);
    remove_hook(me.events.read_conf, filter_conf_hook);

    DMSG(ERR_FILTERCHARONCHAN);
}

#define ANSI_CHAR        '\033'
#define BLINK_CHAR        '\006'
#define BOLD_CHAR        '\002'
#define COLOR_CHAR        '\003'
#define INVERSE_CHAR        '\026'
#define UNDERLINE_CHAR        '\037'

HOOK_FUNCTION(filter_conf_hook) {
    char *s, *arg;

    memset(filter_chars, 0, sizeof(filter_chars));
    /* If not configured, filter only colors and ansi */
    if ((s = conf_find_entry("channel-filter", *ircd.confhead, 1)) == NULL) {
        filter_chars[ANSI_CHAR] = 1;
        filter_chars[COLOR_CHAR] = 1;
        return NULL;
    }

    /* Look at the entry and decide what they want filtered.  The entry is in
     * the form of a word list of the types to be blocked. */
    while ((arg = strsep(&s, " \t,")) != NULL) {
        if (*arg == '\0')
            continue;

        if (!strcasecmp(arg, "ansi"))
            filter_chars[ANSI_CHAR] = 1;
        else if (!strcasecmp(arg, "blink"))
            filter_chars[BLINK_CHAR] = 1;
        else if (!strcasecmp(arg, "bold"))
            filter_chars[BOLD_CHAR] = 1;
        else if (!strcasecmp(arg, "color"))
            filter_chars[COLOR_CHAR] = 1;
        else if (!strcasecmp(arg, "inverse") || !strcasecmp(arg, "reverse"))
            filter_chars[INVERSE_CHAR] = 1;
        else if (!strcasecmp(arg, "underline") || !strcasecmp(arg, "ul"))
            filter_chars[UNDERLINE_CHAR] = 1;
        /* special cases after here */
        else if (!strcasecmp(arg, "control"))
            memset(filter_chars, 1, 32);
        else if (!strncasecmp(arg, "chars:", 6)) {
            char *fc = arg + 6;
            while (*fc != '\0')
                filter_chars[(unsigned char)*fc++] = 1;
        }
    }

    return NULL;
}

/* this function checks the string for blocked control codes and, if it finds
 * any, blocks the message. */
HOOK_FUNCTION(can_send_filter) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;

    if (chanmode_isset(ccap->chan, chanmode_filter)) {
        char *s = ccap->extra;
        while (*s != '\0') {
            if (filter_chars[(unsigned char)*s]) {
                /* blocked character */
                sendto_one(ccap->cli, RPL_FMT(ccap->cli, ERR_FILTERCHARONCHAN),
                        ccap->chan->name);
                return (void *)HOOK_COND_NEVEROK; /* nope. */
            }
            s++;
        }
    }

    return (void *)HOOK_COND_NEUTRAL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
