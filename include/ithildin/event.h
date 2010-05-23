/*
 * event.h: hook/event system structures and prototypes
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: event.h 589 2005-08-25 05:48:38Z wd $
 */

#ifndef EVENT_H
#define EVENT_H

typedef struct event event_t;
typedef void *(*hook_function_t)(event_t *, void *);

/* The event structure.  Intentionally pretty small. */
struct event {
    int            numhooks;                /* the number of hooks in the event */
    int            flags;
#define EVENT_FL_ONEHOOK 0x1        /* allow only one hook at a time */
#define EVENT_FL_NORETURN        0x2        /* return values from hooks are
                                           ignored */
#define EVENT_FL_HOOKONCE        0x4        /* automatically pop each hook off the
                                           stack when it is called */
#define EVENT_FL_CONDITIONAL        0x8        /* do short-circuit conditional
                                           handling using the values below. */
#define EVENT_FL_CALLING        0x10        /* set if we are in the process of
                                           calling this event's hooks.
                                           this defers removal until we are
                                           done with the list. */
    SLIST_HEAD(, hook) hooks;        /* the hooks */
};

#define EVENT_HOOK_COUNT(x) ((x)->numhooks)

/* below are the values for the 'conditional' hooks.  hook_event() will return
 * one of these three values to indicate success or failure.  In general
 * success can be determined by checking for a value less than zero.  If a hook
 * function returns a value greater than zero (typically an error code), it
 * will be assumed as failure and the error code will be returned. */

/* this macro may later become a function.  you should always use
 * hook_cond_event for conditional events. */
#define hook_cond_event(event, data) (int)(hook_event(event, data))

/* This means that the action has been approved, but that not all checks were
 * performed.  This is a special case for success values, as normally even on
 * success all values are checked.  It might be used to indicate an 'override'
 * condition. */
#define HOOK_COND_SPASS -2
/* This means that the action has been approved and that all checks were
 * performed. */
#define HOOK_COND_PASS -1
/* This means that the action has been denied, some checks may not have been
 * performed. */
#define HOOK_COND_FAIL 0

/* The following values should be returned by hooks on conditional events to
 * indicate some measure of success or failure.  The above values *SHOULD NOT*
 * be used as returns in individual hooks! */
#define HOOK_COND_ALWAYSOK  -4        /* short-circuit truth condition */
#define HOOK_COND_OK            -3        /* truth condition */
#define HOOK_COND_NEVEROK   -2        /* short-circuit failure condition */
#define HOOK_COND_NOTOK            -2        /* failure condition */
#define HOOK_COND_NEUTRAL   0        /* neutral (ignored) condition */

/* the simple hook structure.  holds a 'hook_function' and a list pointer
 * for the next item */
struct hook {
    hook_function_t function;
    int flags;
#define HOOK_FL_DEFERRED        0x1        /* 'deferred' for deletion.  set when a
                                           hook would be deleted but its owner
                                           event is being walked */
#define HOOK_FL_NEW                0x2        /* set when a new hook is inserted into
                                           an event being called.  this hook
                                           will not be called on the first pass
                                           through. */

    SLIST_ENTRY(hook) lp;
};

/* init function */
void init_hooksystem(void);

/* event management functions */
event_t *create_event(int);
void **hook_event(event_t *, void *);
void destroy_event(event_t *);

/* hook management functions */
#define add_hook(event, func) add_hook_after((event), (func), NULL)
int add_hook_before(event_t *, hook_function_t, hook_function_t);
int add_hook_after(event_t *, hook_function_t, hook_function_t);
int remove_hook(event_t *, hook_function_t);

/* a simple macro for creating hook functions */
#define HOOK_FUNCTION(func) void *func(event_t *ep __UNUSED,               \
        void *data __UNUSED)

/* this might be erroneous, but I hope not.  set a value for the end of a
 * hook return array that should never be pointed to */
extern int hook_num_returns;

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
