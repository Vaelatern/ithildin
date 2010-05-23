/*
 * protocol.c: rudimentary protocol determination routines
 * 
 * Copyright 2002-2006 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains simple routines to allow connecting hosts to select a
 * protocol for use on the server, or have it otherwise determined for them.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: protocol.c 818 2008-09-21 22:00:54Z wd $");

protocol_t *find_protocol(char *name) {
    protocol_t *pp;

    LIST_FOREACH(pp, ircd.lists.protocols, lp) {
        if (!strcasecmp(pp->name, name))
            return pp;
    }
    return NULL;
}

int add_protocol(char *name) {
    char mname[PATH_MAX];
    protocol_t *pp;

    sprintf(mname, "ircd/protocols/%s", name);
    if (!load_module(mname, MODULE_FL_CREATE|MODULE_FL_QUIET)) {
        log_error("unable to load module for protocol %s", name);
        return 0;
    }
        
    pp = calloc(1, sizeof(protocol_t));
    pp->name = strdup(name);
    pp->dll = find_module(mname);
    LIST_INSERT_HEAD(ircd.lists.protocols, pp, lp);

    update_protocol(pp->name);

    if (pp->input == NULL || pp->output == NULL) {
        remove_protocol(pp->name);
        return 0;
    }

    return 1;
}

/* this function updates the pointers in a protocol (most useful after a
 * reloading of modules */
void update_protocol(char *name) {
    protocol_t *pp = find_protocol(name);
    uint64_t *i64p;

    if (pp == NULL)
        return;

    log_debug("updating protocol %s", pp->name);
    pp->input = (hook_function_t)module_symbol(pp->dll, "input");
    pp->output = (protocol_output_func)module_symbol(pp->dll, "output");
    pp->setup = (void (*)(connection_t *))module_symbol(pp->dll, "setup");

    /* not all protocols will have these (specifically, client protocols will
     * not have them) */
    pp->register_user = (void (*)(connection_t *, client_t *))
        module_symbol(pp->dll, "register_user");
    pp->sync_channel = (void (*)(connection_t *, channel_t *))
        module_symbol(pp->dll, "sync_channel");

    if (pp->register_user != NULL || pp->sync_channel != NULL) {
        if (pp->register_user == NULL || pp->sync_channel == NULL)
            log_error("missing protocol functions in module for "
                    "protocol %s", name);
    }

    if (pp->input == NULL || pp->output == NULL)
        log_error("missing input or output function in module for "
                "protocol %s", name);

    if ((i64p = (uint64_t *)module_symbol(pp->dll, "protocol_flags")) != NULL)
        pp->flags = *i64p;

    if ((i64p = (uint64_t *)module_symbol(pp->dll, "protocol_buffer_size")) != NULL)
        pp->bufsize = *i64p;
    else
        pp->bufsize = 512;
}

/* this removes a protocol.  basically it closes all connections in that
 * protocol and then removes it from the list. */
void remove_protocol(char *name) {
    protocol_t *pp = find_protocol(name);
    connection_t *cp, *cp2;

    if (pp == NULL)
        return;

    if (pp->dll->flags & MODULE_FL_RELOADING)
        return; /* don't do this here.. */

    cp = LIST_FIRST(ircd.connections.stage1);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        if (cp->proto == pp)
            destroy_connection(cp, "protocol removed");
        cp = cp2;
    }
    cp = LIST_FIRST(ircd.connections.stage2);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        if (cp->proto == pp)
            destroy_connection(cp, "protocol removed");
        cp = cp2;
    }

    cp = LIST_FIRST(ircd.connections.clients);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        if (cp->proto == pp)
            destroy_client(cp->cli, "protocol removed");
        cp = cp2;
    }
    cp = LIST_FIRST(ircd.connections.servers);
    while (cp != NULL) {
        cp2 = LIST_NEXT(cp, lp);
        if (cp->proto == pp)
            destroy_server(cp->srv, "protocol removed");
        cp = cp2;
    }

    LIST_REMOVE(pp, lp);
    free(pp->name);
    free(pp);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
