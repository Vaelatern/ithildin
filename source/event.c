/*
 * event.c: the event/hook system support functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains functions for managing events and their hooks.  The
 * system is relatively simple.  An event is created, and hooks are added to
 * it.  Then the event is triggered and the hooks for that event are called
 * one-by-one.  The system is deliberately simple to allow extremely fast
 * hooking of events.
 */

#include <ithildin/stand.h>

IDSTRING(rcsid, "$Id: event.c 590 2005-08-25 06:09:14Z wd $");

/* internally we the maximum number of hooks of any event in the system and
 * an array used for passing returned values from hooks respectively */
int maxhooks;
void **hookreturns;
int hook_num_returns;

void add_hook_really(event_t *ep, hook_function_t func, struct hook *at);
struct hook *find_hook(event_t *ep, hook_function_t func);

void init_hooksystem(void) {
    maxhooks = 16;
    hookreturns = malloc(sizeof(void *) * maxhooks);
}

event_t *create_event(int flags) {
    event_t *ep = NULL;
        
    ep = malloc(sizeof(event_t));
    ep->numhooks = 0;
    ep->flags = flags;

    SLIST_INIT(&ep->hooks);

    return ep;
}

/* this is the only really major function in the code, and will be the one
 * which gets called by far the most often.  try to make it as fast as
 * possible while being as robust as possible.  basically we call each hook
 * for an event, and unless the event is such that return data is ignored,
 * we build the return list along with it.  assume that data will NOT be
 * modified by each function, although it is 'possible' that this might
 * happen (but it's not our problem :). */
void **hook_event(event_t *ep, void *data) {
    struct hook *hp, *hp2;
    void **returned;
    void *ret, *econd = NULL;
    int i = 0;
    bool needclean = false;

    /* set the calling flag.. deletions in this event will now be deferred */
    ep->flags |= EVENT_FL_CALLING;

    if (ep->flags & EVENT_FL_NORETURN)
        returned = NULL;
    else if (ep->flags & EVENT_FL_CONDITIONAL)
        returned = (void **)HOOK_COND_PASS; /* success is the default */
    else
        returned = hookreturns;
        SLIST_FOREACH(hp, &ep->hooks, lp) {
        if (hp->flags & (HOOK_FL_DEFERRED | HOOK_FL_NEW)) {
            needclean = true;
            continue; /* either deferred for deletion or newly added */
        }

        ret = hookreturns[i++] = hp->function(ep, data);
        if (ep->flags & EVENT_FL_CONDITIONAL) {
            if (ret == (void *)HOOK_COND_ALWAYSOK) {
                /* short-circuit success value.  stop here */
                returned = (void **)HOOK_COND_SPASS;
                break;
            } else if (ret == (void *)HOOK_COND_NEVEROK) {
                /* short-circuit failure value.  stop here */
                returned = (void **)HOOK_COND_FAIL;
                break;
            } else if (ret == (void *)HOOK_COND_NOTOK)
                returned = (void **)HOOK_COND_FAIL;
            else if (ret != (void *)HOOK_COND_OK &&
                    ret != (void *)HOOK_COND_NEUTRAL) {
                returned = (void **)HOOK_COND_FAIL;
                econd = ret;
            }
        }

        if (ep->flags & EVENT_FL_HOOKONCE) {
            hp->flags |= HOOK_FL_DEFERRED;
            needclean = true;
        }
    }

    if (ep->flags & EVENT_FL_CONDITIONAL) {
        /* if we got a positive return (econd is set) then set returned
         * properly. */
        if (returned == (void **)HOOK_COND_FAIL && econd != NULL)
            returned = (void **)econd;
    } else
        hook_num_returns = i;

    /* trash the hooks which are deferred (will be all of them if this is a
     * 'hookonce' event. */
    ep->flags &= ~EVENT_FL_CALLING;

    if (needclean) {
        hp = SLIST_FIRST(&ep->hooks);
        while (hp != NULL) {
            hp2 = SLIST_NEXT(hp, lp);

            if (hp->flags & HOOK_FL_DEFERRED) {
                SLIST_REMOVE(&ep->hooks, hp, hook, lp);
                free(hp);
            } else if (hp->flags & HOOK_FL_NEW)
                hp->flags &= ~HOOK_FL_NEW;
            hp = hp2;
        }
    }

    return returned;
}
                
void destroy_event(event_t *ep) {
    struct hook *hp;

    if (ep == NULL)
        return;

    while (!SLIST_EMPTY(&ep->hooks)) {
        hp = SLIST_FIRST(&ep->hooks);
        SLIST_REMOVE_HEAD(&ep->hooks, lp);
        free(hp);
    }
    free(ep);
}


/* add a hook after the specified one.  if after is NULL, it is added as the
 * last hook on the event. */
int add_hook_after(event_t *ep, hook_function_t func,
        hook_function_t after) {
    struct hook *hp = NULL;

    if (ep == NULL || func == NULL)
        return 0;

    if (ep->numhooks && ep->flags & EVENT_FL_ONEHOOK) {
        log_debug("tried to add another hook to a one-hook event");
        return 0;
    }

    /* find the hook they're after.  add it on to that.  if we can't find the
     * hook they're after, add it onto the end.  if they specify a NULL func,
     * add it on to the end. */
    if (after == NULL) {
        SLIST_FOREACH(hp, &ep->hooks, lp) {
            if (SLIST_NEXT(hp, lp) == NULL)
                break;
        }
    } else {
        SLIST_FOREACH(hp, &ep->hooks, lp) {
            if (hp->function == after)
                break; /* found it */
            if (SLIST_NEXT(hp, lp) == NULL)
                break; /* if the list ends here, be sure to preserve this */
        }
    }

    add_hook_really(ep, func, hp);
    return 1;
}

int add_hook_before(event_t *ep, hook_function_t func,
        hook_function_t before) {
    struct hook *hp = NULL;
    struct hook *hp2 = NULL;

    if (ep == NULL || func == NULL)
        return 0;

    if (ep->numhooks && ep->flags & EVENT_FL_ONEHOOK) {
        log_debug("tried to add another hook to a one-hook event");
        return 0;
    }

    /* find the hook they're after.  add it on to the one before that.  if we
     * can't find it we add it on to the end.  If they specify NULL, the list
     * is empty, or the first entry is the one to insert before, we add it at
     * the head of the list. */
    if (before == NULL || SLIST_EMPTY(&ep->hooks) ||
            ((SLIST_FIRST(&ep->hooks))->function == before))
        hp = NULL;
    else {
        SLIST_FOREACH(hp, &ep->hooks, lp) {
            hp2 = SLIST_NEXT(hp, lp);
            if (hp2 == NULL)
                break; /* add it on to the end */
            else if (hp2->function == before)
                break; /* hp is the one before 'function'.  tada */
        }
    }

    add_hook_really(ep, func, hp);
    return 1;
}

void add_hook_really(event_t *ep, hook_function_t func, struct hook *at) {
    struct hook *hp;

    hp = malloc(sizeof(struct hook));
    hp->function = func;
    if (ep->flags & EVENT_FL_CALLING)
        hp->flags = HOOK_FL_NEW;
    else
        hp->flags = 0;
    if (at == NULL)
        SLIST_INSERT_HEAD(&ep->hooks, hp, lp);
    else
        SLIST_INSERT_AFTER(at, hp, lp);
    if (maxhooks <= ++ep->numhooks) {
        maxhooks *= 2;
        hookreturns = (void **)realloc(hookreturns, sizeof(void *) * maxhooks);
    }
}

struct hook *find_hook(event_t *ep, hook_function_t func) {
    struct hook *hp = NULL;

    SLIST_FOREACH(hp, &ep->hooks, lp) {
        if (hp->function == func)
            return hp;
    }

    return NULL;
}

int remove_hook(event_t *ep, hook_function_t func) {
    struct hook *hp = find_hook(ep, func);

    if (hp != NULL) {
        ep->numhooks--;

        if (ep->flags & EVENT_FL_CALLING)
            hp->flags |= HOOK_FL_DEFERRED; /* just defer this for deletion */
        else {
            SLIST_REMOVE(&ep->hooks, hp, hook, lp);
        free(hp);
        }
        return 1;
    }

    return 0;
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
