/*
 * timer.c: the timer system functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains the functions necessary to create and manage timers.
 * Currently timers have only second-level resolution (i.e. there are no
 * sub-second timers).  Timers are referred to using a 64-bit reference id.
 * This allows for 2^64 timers to be created before there is any danger of
 * overlap.
 */

#include <ithildin/stand.h>

IDSTRING(rcsid, "$Id: timer.c 578 2005-08-21 06:37:53Z wd $");

/* this stores the reference count for timers and whether or not the reference
 * count has rolled over. */
static timer_ref_t timer_refcnt;
bool timer_rollover;

#define TIMER_DEAD -1
#define TIMER_REPEAT -2

static timer_event_t *find_timer(timer_ref_t);
static inline void insert_timer(timer_event_t *);

/* This creates a new timer which repeats for the given count (0 means no
 * repetition, negative values mean infinite repetition) at the given interval
 * calling the given function at each execution of the timer.  It returns the
 * reference id of the timer so that it can be destroyed later if necessary.
 * The reference id is given to avoid passing back a timer_event structure that
 * may disappear and become invalid during execution. */
timer_ref_t create_timer(int rep, time_t interval, hook_function_t callback,
        void *udata) {
    timer_event_t *tep = malloc(sizeof(timer_event_t));

    if (timer_rollover) {
        /* if the timer ref count has rolled over we need to make sure we
         * assign a unique reference id. */
        tep->ref = timer_refcnt++;
        while (find_timer(tep->ref) != NULL)
            tep->ref = timer_refcnt++;
    } else {
        tep->ref = timer_refcnt++;
        if (timer_refcnt == 0)
            timer_rollover = true;
        if (tep->ref == TIMER_INVALID) {
            free(tep);
            return create_timer(rep, interval, callback, udata);
        }
    }

    if (rep < 0)
        tep->reps = TIMER_REPEAT;
    else
        tep->reps = rep;
    tep->interval = interval;
    tep->next = me.now + tep->interval;
    tep->callback = callback;
    tep->udata = udata;

    insert_timer(tep);

    return tep->ref;
}

/* This function looks for a timer with the given reference id and returns it
 * if it is found. */
static timer_event_t *find_timer(timer_ref_t ref) {
    timer_event_t *tep;

    LIST_FOREACH(tep, &me.timers, lp) {
        if (tep->ref == ref)
            return tep;
    }

    return NULL;
}

/* This destroys the timer with the given reference id.  It is possible (but
 * rather unlikely) that the wrong timer can be destroyed here because of a
 * previously assigned reference id. */
void destroy_timer(timer_ref_t ref) {
    timer_event_t *tep = find_timer(ref);

    if (tep == NULL)
        return;

    LIST_REMOVE(tep, lp);
    free(tep);
}

/* this allows the caller to adjust the settings of a timer (specifically the
 * repeat count and the time it goes off).  the third argument is the execution
 * time from 'now' (the current time). */
void adjust_timer(timer_ref_t tref, int reps, time_t interval) {
    timer_event_t *tep = find_timer(tref);

    if (tep == NULL)
        return;

    LIST_REMOVE(tep, lp);
    tep->reps = (reps < 0 ? TIMER_REPEAT : reps);
    tep->interval = interval;
    tep->next = me.now + interval;
    insert_timer(tep);
}

/* This function executes each timer that needs to be called and re-orders the
 * list as necessary. */
time_t exec_timers(void) {
    timer_event_t *tep, *tep2;

    tep = LIST_FIRST(&me.timers);
    me.now = time(NULL);
    while (tep != NULL) {
        tep2 = LIST_NEXT(tep, lp);
        if (tep->next <= me.now) {
            tep->callback(NULL, tep->udata);
            /* now see if the timer needs to be deleted.  if not, then we
             * re-calculate next and re-position the timer in the list. */
            if (tep->reps != TIMER_REPEAT && --tep->reps == TIMER_DEAD)
                destroy_timer(tep->ref);
            else {
                if ((tep->next += tep->interval) < me.now)
                    tep->next = me.now + tep->interval;
                LIST_REMOVE(tep, lp);
                insert_timer(tep);
            }
        } else
            /* the list is sorted, so if the condition above is false all the
             * timers at and beyond this point will not need to go off at this
             * go-around */
            break;
        tep = tep2;
        me.now = time(NULL);
    }

    /* We've executed all the timers we needed to... see when the next one (if
     * any) will need to go off. */
    if (LIST_EMPTY(&me.timers))
        return 0;
    else
        return ((timer_event_t *)LIST_FIRST(&me.timers))->next - me.now;
}

/* This inserts a timer into the correct position in the timer list.  That is,
 * it sorts the timer list by order of execution. */
static inline void insert_timer(timer_event_t *timer) {
    timer_event_t *tep;

    tep = LIST_FIRST(&me.timers);
    if (tep == NULL) {
        LIST_INSERT_HEAD(&me.timers, timer, lp);
        return;
    }

    while (tep != NULL) {
        if (tep->next > timer->next) {
            LIST_INSERT_BEFORE(tep, timer, lp);
            return;
        }
        if (LIST_NEXT(tep, lp) == NULL)
            break;
        tep = LIST_NEXT(tep, lp);
    }
    LIST_INSERT_AFTER(tep, timer, lp);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
