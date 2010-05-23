/*
 * module.c: the MODULE command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/core.h"

IDSTRING(rcsid, "$Id: module.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/core
*/

/* this command may be used by operators to load, unload, or reload modules
 * argv[1] == load | unload | reload | list
 * argv[2] ?= type (forward slashes may be replaced by dashes)
 * argv[3] ?= module name */
CLIENT_COMMAND(module, 0, 3, COMMAND_FL_OPERATOR) {
    char *type = "*"; /* default to all */
    char mname[PATH_MAX];
    module_t *mp;
#define MODLIST 1
#define MODLOAD 2
#define MODUNLOAD 3
#define MODRELOAD 4
    int op = 0;

    if (argc == 1)
        op = MODLIST;
    else if (!strcasecmp(argv[1], "list"))
        op = MODLIST;
    else if (!strcasecmp(argv[1], "load"))
        op = MODLOAD;
    else if (!strcasecmp(argv[1], "unload"))
        op = MODUNLOAD;
    else if (!strcasecmp(argv[1], "reload"))
        op = MODRELOAD;

    if (argc > 2) {
        char *s;

        s = type = argv[2];
        while (*s != '\0') {
            if (*s == '-')
                *s = '/';
            s++;
        }
    }

    /* hmpf.  if they're not listing, they need to specify a module name or at
     * the very least a pattern. */
    if (op == 0 || (op != MODLIST && argc < 4)) {
        sendto_one(cli, RPL_FMT(cli, RPL_COMMANDSYNTAX),
                "MODULE <list|load|unload|reload> [type] [name]");
        return COMMAND_WEIGHT_NONE;
    }
    if (!strcmp(type, "*") || !strcasecmp(type, "all"))
        sprintf(mname, "%s", argc > 3 ? argv[3] : "*");
    else
        sprintf(mname, "%s/%s", type, (argc > 3 ? argv[3] : "*"));

    /* depending on what they asked for, we do different things.  the one
     * special case here is that if a module load is requested and a mask is
     * NOT given we try an explicit load and give them the results on the spot,
     * then return. */
    if (op == MODLOAD && strchr(mname, '*') == NULL &&
            strchr(mname, '?') == NULL) {
        sendto_flag(ircd.sflag.ops, "%s requested that module %s be loaded",
                cli->nick, mname);
        if (load_module(mname, MODULE_FL_CREATE))
            sendto_one(cli, "NOTICE", ":loaded module %s successfully", mname);
        else
            sendto_one(cli, "NOTICE", ":could not load module %s", mname);

        return COMMAND_WEIGHT_NONE;
    }

    /* we have to walk the modules list ourself because the API for modules
     * doesn't including matching.. */
    LIST_FOREACH(mp, &me.modules, lp) {
        if (!match(mname, mp->name))
            continue; /* move along.. */

        if (op == MODUNLOAD && (!strcasecmp(mp->name, "dns") ||
                    !strcasecmp(mp->name, "ident") ||
                    !strcasecmp(mp->name, "ircd") ||
                    !strcasecmp(mp->name, "ircd/commands/module"))) {
            sendto_one(cli, "NOTICE", ":You cannot unload the %s module",
                    mp->name);
            continue;
        }

        switch (op) {
            case MODLIST:
                sendto_one(cli, "NOTICE", ":module %s (%s) loaded at addr %p",
                        mp->name, mp->fullpath, mp->handle);
                break;
            case MODLOAD:
                sendto_flag(ircd.sflag.ops,
                    "%s requested that module %s be loaded", cli->nick, mname);
                if (load_module(mp->name, 0))
                    sendto_one(cli, "NOTICE", ":loaded module %s successfully",
                            mp->name);
                else
                    sendto_one(cli, "NOTICE", ":could not load module %s",
                            mp->name);
                break;
            case MODUNLOAD:
                sendto_flag(ircd.sflag.ops,
                        "%s requested that module %s be unloaded", cli->nick,
                        mp->name);
                if (unload_module(mp->name))
                    sendto_one(cli, "NOTICE",
                            ":unloaded module %s successfully", mp->name);
                else
                    sendto_one(cli, "NOTICE", ":could not unload module %s",
                            mp->name);
                break;
            case MODRELOAD:
                sendto_flag(ircd.sflag.ops,
                        "%s requested that module %s be reloaded", cli->nick,
                        mp->name);
                if (reload_module(mp->name))
                    sendto_one(cli, "NOTICE",
                            ":reloaded module %s successfully", mp->name);
                else
                    sendto_one(cli, "NOTICE", ":could not reload module %s",
                            mp->name);
                break;
        }
    }

    return COMMAND_WEIGHT_NONE;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
