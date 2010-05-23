/*
 * class.c: connection class management functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file provides basic functions for adding and removing users from
 * connection classes, and finding connection classes by name.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: class.c 579 2005-08-21 06:38:18Z wd $");

class_t *create_class(char *name) {
    class_t *c = malloc(sizeof(class_t));

    memset(c, 0, sizeof(class_t));
    strlcpy(c->name, name, 32);
    c->freq = 180;
    c->max = 600;
    c->flood = 192; /* ? */
    c->clients = 0;
    c->sendq = 1000; /* ? */
    c->default_mode = strdup("+i");
    c->mset = LIST_FIRST(ircd.messages.sets);
    c->pset = LIST_FIRST(ircd.privileges.sets);
    c->dead = 0;

    c->mdext = mdext_alloc(ircd.mdext.class);

    if (LIST_EMPTY(ircd.lists.classes))
        LIST_INSERT_HEAD(ircd.lists.classes, c, lp);
    else
        LIST_INSERT_AFTER(LIST_FIRST(ircd.lists.classes), c, lp);

    return c;
}

void destroy_class(class_t *c) {

    if (c == LIST_FIRST(ircd.lists.classes))
        return; /* don't nerf the default class */

    c->dead = 1;
    if (c->clients)
        return; /* don't destroy it if it has clients. */

    log_debug("expiring dead connection class \"%s\"", c->name);
    LIST_REMOVE(c, lp);
    mdext_free(ircd.mdext.class, c->mdext);
    free(c->default_mode);
    free(c);
}

void add_to_class(class_t *c, connection_t *conn) {

    if (conn->cls == c)
        return; /* no change */

    /* just in case.. */
    if (conn->cls != NULL)
        del_from_class(conn);

    conn->cls = c;
    conn->mset = c->mset;
    if (conn->cli != NULL)
        conn->cli->pset = c->pset;
    c->clients++;
}

void del_from_class(connection_t *conn) {
    class_t *c = conn->cls;

    c->clients--;
    conn->cls = NULL; /* make sure nothing references this class */

    if (c->clients == 0 && c->dead) /* if it's dead, nerf it */
        destroy_class(c);
}

class_t *find_class(char *name) {
    class_t *c;

    LIST_FOREACH(c, ircd.lists.classes, lp) {
        if (!strcasecmp(name, c->name))
            return c;
    }

    return NULL;
}

char **class_mdext_iter(char **last) {
    static int started = 0;
    class_t *cp = *(class_t **)last;

    if (cp == NULL) {
        if (started) {
            started = 0;
            return NULL;
        }
        cp = LIST_FIRST(ircd.lists.classes);
        started = 1;
    }
    if (cp == NULL)
        return NULL;

    *(class_t **)last = LIST_NEXT(cp, lp);
    return (char **)&cp->mdext;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
