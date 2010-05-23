/*
 * antidrone.c: Various anti drone combatants.
 * 
 * Copyright 2006 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * Various simple methods for detecting and deterring drones all rolled into
 * a single module.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: cmode_strip.c 742 2006-05-31 01:13:09Z wd $");

MODULE_REGISTER("$Rev: 742 $");

/*
@DEPENDENCIES@:        ircd
*/

static bool anti_bear = false;
static int anti_bear_message;

HOOK_FUNCTION(antidrone_conf_hook);
HOOK_FUNCTION(antidrone_rc_hook);

MODULE_LOADER(antidrone) {

    add_hook_before(ircd.events.register_client, antidrone_rc_hook, NULL);
    add_hook(me.events.read_conf, antidrone_conf_hook);

    anti_bear_message = create_message("antidrone-bear-message",
            "Please ignore this test message.");

    antidrone_conf_hook(NULL, NULL);
    return 1;
}

MODULE_UNLOADER(antidrone) {
    
    remove_hook(ircd.events.register_client, antidrone_rc_hook);
    remove_hook(me.events.read_conf, antidrone_conf_hook);

    destroy_message(anti_bear_message);
}


HOOK_FUNCTION(antidrone_conf_hook) {

    /* look for our various entries */
    anti_bear = str_conv_bool(conf_find_entry("antidrone-bear",
                *ircd.confhead, 1), false);

    return NULL;
}

/* Send various messages to connecting clients based on the configured
 * parameters. */
HOOK_FUNCTION(antidrone_rc_hook) {
    client_t *cli = (client_t *)data;

    if (!MYCLIENT(cli))
        return NULL; /* do not bother processing remote clients */

    if (anti_bear)
        sendto_one(cli, "439", ":%s", MSG_FMT(cli, anti_bear_message));

    return NULL;
}


/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
