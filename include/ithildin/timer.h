/*
 * timer.h: timer creation system.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: timer.h 578 2005-08-21 06:37:53Z wd $
 */

#ifndef TIMER_H
#define TIMER_H

typedef struct timer_event timer_event_t;
typedef uint64_t timer_ref_t;
#define TIMER_INVALID (uint64_t)0xffffffffffffffffLL

/* The timer structure.  This contains the function to call back, and various
 * data values pertaining to execution time. */
struct timer_event {
    timer_ref_t ref;                /* timer reference number */
    int            reps;                /* number of times to repeat this call.  a
                                   negative value indicates indefinite
                                   repetition. */
    time_t  interval;                /* interval between calls. */
    time_t  next;                /* the (absolute) next time this call should be
                                   made. */
    void    *udata;                /* data value passed with the callback */
    hook_function_t callback;        /* the callback function */

    LIST_ENTRY(timer_event) lp;
};

timer_ref_t create_timer(int, time_t, hook_function_t, void *);
void destroy_timer(timer_ref_t);
void adjust_timer(timer_ref_t, int, time_t);
time_t exec_timers(void);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
