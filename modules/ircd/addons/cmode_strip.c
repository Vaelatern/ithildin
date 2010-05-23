/*
 * cmode_strip.c: control-character stripping mode
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module adds character removal for specified characters in channel
 * messages.  It does not block any messages, but edits them in-place.
 *
 * XXX: This is really rotten.  It's not just content moderation, it is content
 * *modification* which is really bad.  In keeping with other implementations
 * it does *not* inform the user that the content of their messages is being
 * altered.  I personally don't advise the use of this mode, and instead advise
 * you to look to the cmode_filter addon, but if this is what you really want
 * then go ahead and use it.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"

IDSTRING(rcsid, "$Id: cmode_strip.c 768 2006-09-07 23:03:55Z wd $");

MODULE_REGISTER("$Rev: 768 $");

/*
@DEPENDENCIES@:        ircd
@DEPENDENCIES@: ircd/addons/core
*/

static unsigned char chanmode_strip;
static char strip_chars[256];

HOOK_FUNCTION(can_send_strip);
HOOK_FUNCTION(strip_conf_hook);

MODULE_LOADER(cmode_strip) {

    if (!get_module_savedata(savelist, "chanmode_strip", &chanmode_strip))
        chanmode_request('c', &chanmode_strip, CHANMODE_FL_D,
                "chanmode_flag", "chanmode_flag_query", 0, NULL);

    add_hook_before(ircd.events.can_send_channel, can_send_strip, NULL);
    add_hook(me.events.read_conf, strip_conf_hook);

    strip_conf_hook(NULL, NULL);
    return 1;
}

MODULE_UNLOADER(cmode_strip) {
    
    if (reload)
        add_module_savedata(savelist, "chanmode_strip",
                sizeof(chanmode_strip), &chanmode_strip);
    else
        chanmode_release(chanmode_strip);

    remove_hook(ircd.events.can_send_channel, can_send_strip);
    remove_hook(me.events.read_conf, strip_conf_hook);
}

#define ANSI_CHAR       '\033'
#define BLINK_CHAR      '\006'
#define BOLD_CHAR       '\002'
#define COLOR_CHAR      '\003'
#define INVERSE_CHAR    '\026'
#define UNDERLINE_CHAR  '\037'

HOOK_FUNCTION(strip_conf_hook) {
    char *s, *arg;

    memset(strip_chars, 0, sizeof(strip_chars));
    /* If not configured, strip only colors and ansi */
    if ((s = conf_find_entry("channel-strip", *ircd.confhead, 1)) == NULL) {
        strip_chars[ANSI_CHAR] = 1;
        strip_chars[COLOR_CHAR] = 1;
        return NULL;
    }

    /* Look at the entry and decide what they want striped.  The entry is in
     * the form of a word list of the types to be blocked. */
    while ((arg = strsep(&s, " \t,")) != NULL) {
        if (*arg == '\0')
            continue;

        if (!strcasecmp(arg, "ansi"))
            strip_chars[ANSI_CHAR] = 1;
        else if (!strcasecmp(arg, "blink"))
            strip_chars[BLINK_CHAR] = 1;
        else if (!strcasecmp(arg, "bold"))
            strip_chars[BOLD_CHAR] = 1;
        else if (!strcasecmp(arg, "color"))
            strip_chars[COLOR_CHAR] = 1;
        else if (!strcasecmp(arg, "inverse") || !strcasecmp(arg, "reverse"))
            strip_chars[INVERSE_CHAR] = 1;
        else if (!strcasecmp(arg, "underline") || !strcasecmp(arg, "ul"))
            strip_chars[UNDERLINE_CHAR] = 1;
        /* special cases after here */
        else if (!strcasecmp(arg, "control"))
            memset(strip_chars, 1, 32);
        else if (!strncasecmp(arg, "chars:", 6)) {
            char *fc = arg + 6;
            while (*fc != '\0')
                strip_chars[(unsigned char)*fc++] = 1;
        }
    }

    return NULL;
}

/* This function goes through every string sent and modifies the content as
 * necessary.  Blech blech BLECH!  Also, in the case of mIRC colors (character
 * \003) we go a step further and try to pass over the entire color string
 * which is of the form '\003[n[n[,[n[n]]]]]' where 'n' is some number. */
HOOK_FUNCTION(can_send_strip) {
    struct channel_check_args *ccap = (struct channel_check_args *)data;

    if (chanmode_isset(ccap->chan, chanmode_strip)) {
        unsigned char *from, *to;
        from = to = (unsigned char *)ccap->extra;

        while (*from != '\0') {
            if (strip_chars[*from]) {
                /* We will always skip the character here, it just may not
                 * match COLOR_CHAR in which case we simply continue on */
                if (*from++ == COLOR_CHAR) {
                    if (isdigit(*from)) {
                        if (isdigit(*++from))
                            from++; /* skip second digit which is optional */

                        if (*from == ',' && isdigit(*(from + 1))) {
                            /* background color specified ... */
                            from += 2;
                            if (isdigit(*from))
                                from++;
                        }
                    }
                }
            } else
                *to++ = *from++;
        }
        *to = '\0';
    }

    return (void *)HOOK_COND_NEUTRAL;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
