/*
 * throttle.c: connection throttling.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module adds 'connection throttling' support to the server.  Basically,
 * it watches client connects and penalizes rapid connections from the same
 * address.  This is not a replacement for clone monitoring, just a way to make
 * sure abusive clients/bots do not monopolize system resources.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/acl.h"

IDSTRING(rcsid, "$Id: throttle.c 732 2006-05-12 03:38:09Z wd $");

MODULE_REGISTER("$Rev: 732 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/addons/acl
*/

static const char *throttle_acl_type = "throttle";

typedef struct throttle throttle_t;
struct throttle {
    char addr[IPADDR_SIZE];         /* IP address of connection */
    time_t first;                   /* when they started connecting.. */
    int count;                      /* number of connections made within
                                       'throttle.span' time. */
    time_t span;                    /* "span" time, essentially the length
                                       of time we monitor "count" over. */
    time_t banned;                  /* if they're banned, this is when we added
                                       the acl. */
    timer_ref_t timer;              /* the timer for this throttle */
    int stage;                      /* 'stage' they are throttled at.  0 means
                                       unthrottled. */
    LIST_ENTRY(throttle) lp;        /* the list entry */
};

#define THROTTLE_MAXCONNS 3
#define THROTTLE_SPAN 15
#define THROTTLE_STAGES 5
#define THROTTLE_CACHE 3600
static struct {
    int     conns;      /* number of connections before IP is throttled.. */
    time_t  span;       /* if they occur in this many seconds */
    time_t  *lengths;   /* lengths for throttles.  one per stage. */
    int     stages;     /* maximum number of throttle stages.  each throttle
                           stage is two times the length of the previous. */
    time_t  cache;      /* time to cache throttles for. */
    hashtable_t *table;
    LIST_HEAD(, throttle) *list;
} throttle;

static throttle_t *create_throttle(struct isock_address *);
static throttle_t *find_throttle(struct isock_address *);
static void destroy_throttle(throttle_t *);

HOOK_FUNCTION(throttle_stage1_hook);
HOOK_FUNCTION(throttle_conf_hook);
HOOK_FUNCTION(throttle_timer_hook);

static throttle_t *create_throttle(struct isock_address *addr) {
    throttle_t *tp = calloc(1, sizeof(throttle_t));
    char ipstr[IPADDR_MAXLEN + 1];

    tp->span = throttle.span; /* Set a default span */

    tp->timer = create_timer(0, throttle.cache, throttle_timer_hook, tp);
    /* we have to take the long road to get the packed IP address. */
    get_socket_address(addr, ipstr, IPADDR_MAXLEN + 1, NULL);
    inet_pton(addr->family, ipstr, tp->addr);

    LIST_INSERT_HEAD(throttle.list, tp, lp);
    hash_insert(throttle.table, tp);

    return tp;
}

static throttle_t *find_throttle(struct isock_address *addr) {
    char ipstr[IPADDR_MAXLEN + 1];
    char ip[IPADDR_SIZE];

    get_socket_address(addr, ipstr, IPADDR_MAXLEN + 1, NULL);
    memset(ip, 0, IPADDR_SIZE);
    inet_pton(addr->family, ipstr, ip);

    return hash_find(throttle.table, ip);
}

static void destroy_throttle(throttle_t *tp) {

    if (tp->timer != TIMER_INVALID)
        destroy_timer(tp->timer);
    LIST_REMOVE(tp, lp);
    hash_delete(throttle.table, tp);
    free(tp);
}

/* this is the ERROR message for throttles.  Apparently some clients (like
 * mIRC) will stop trying to reconnect if they get this in an ERROR. */
#define THROTTLE_ERRMSG                                                        \
    "Your host is trying to (re)connect too fast -- throttled."
HOOK_FUNCTION(throttle_stage1_hook) {
    connection_t *cp = (connection_t *)data;
    throttle_t *tp = find_throttle(isock_raddr(cp->sock));
    acl_t *ap;
    time_t len = 0;

    if (tp == NULL)
        tp = create_throttle(isock_raddr(cp->sock));
    if (me.now - tp->first > throttle.span) {
        tp->first = me.now;
        tp->count = 0;
    }

    /* See if they're banned, take it off if they are. */
    if (tp->banned != 0 && tp->banned + throttle.lengths[tp->stage - 1] < me.now) {
        /* they're no longer banned, so set first to now and count to 1 */
        tp->banned = 0;
        tp->first = me.now;
        tp->count = 0;
        tp->span = throttle.span;
        adjust_timer(tp->timer, 0, throttle.cache);
    }

    tp->count++;
    if (tp->count < throttle.conns) {
        /* We do not do the below processing except when the count cycles
         * back above the 'throttle.conns' level again, still, if they're
         * banned we should say so. */
        if (tp->banned != 0)
            return THROTTLE_ERRMSG;
        else
            return NULL;
    }

    /* If we continue it is because we are throttling them (or changing the
     * parameters of the throttling. */

    if (tp->banned == 0) {
        tp->banned = me.now; /* They are now banned.. */
        tp->stage = 0; /* Will get incremented below */
    }

    /* Unset 'first' and 'count'.  The count will be redone for the next
     * stage of bannging, as necessary. */
    tp->first = 0;
    tp->count = 0;

    /* they've already been banned.  figure out how long they should
     * have been banned for, and if the ban hasn't expired, get the acl
     * for it (or add a new one) and extend the time if necessary. */
    if (tp->stage < throttle.stages)
        tp->stage++;
    len = throttle.lengths[tp->stage - 1];
    if (len > throttle.cache)
        adjust_timer(tp->timer, 0, len);
    tp->span = len; /* change the span */

    if (tp->banned + len >= me.now)  {
        if ((ap = find_acl(ACL_STAGE_CONNECT, ACL_DENY, cp->host,
                        throttle_acl_type, ACL_DEFAULT_RULE, NULL, NULL)) == NULL) {
            ap = create_acl(ACL_STAGE_CONNECT, ACL_DENY, cp->host,
                    throttle_acl_type, ACL_DEFAULT_RULE);
            ap->reason = strdup(THROTTLE_ERRMSG);
        }
        ap->conf = ACL_CONF_TEMP;
        ap->added = me.now; /* update the 'added' time. */
        acl_add_timer(ap, len);
    }

    return THROTTLE_ERRMSG;
}
             

HOOK_FUNCTION(throttle_conf_hook) {
    conf_list_t *clp;
    char *s;
    int i;

    /* free resources .. */
    if (throttle.lengths != NULL) {
        free(throttle.lengths);
        throttle.lengths = NULL;

        while (!LIST_EMPTY(throttle.list))
            destroy_throttle(LIST_FIRST(throttle.list));
    }

    /* if we have no configuration data then just fill in defaults. */
    if ((clp = conf_find_list("throttle", *ircd.confhead, 1)) == NULL) {
        /* just set defaults */
        throttle.conns = THROTTLE_MAXCONNS;
        throttle.span = THROTTLE_SPAN;
        throttle.stages = THROTTLE_STAGES;
        throttle.cache = THROTTLE_CACHE;

        throttle.lengths = malloc(sizeof(time_t) * throttle.stages);
        for (i = 0;i < throttle.stages;i++) {
            if (i == 0)
                throttle.lengths[i] = 180;
            else
                throttle.lengths[i] = throttle.lengths[i - 1] * (i + 1);
        }

        return NULL;
    }

    throttle.conns = str_conv_int(conf_find_entry("trigger-count", clp, 1),
            THROTTLE_MAXCONNS);
    throttle.span = str_conv_time(conf_find_entry("trigger-time", clp, 1),
            THROTTLE_SPAN);
    throttle.stages = str_conv_int(conf_find_entry("stages", clp, 1),
            THROTTLE_STAGES);
    throttle.cache = str_conv_time(conf_find_entry("cache-length", clp, 1),
            THROTTLE_CACHE);

    /* now get the lengths.  if a length isn't configured it is assumed to be
     * the initial length times the length previous. */
    throttle.lengths = malloc(sizeof(time_t) * throttle.stages);
    for (i = 0;i < throttle.stages;i++) {
        if (i == 0)
            throttle.lengths[i] = 180;
        else
            throttle.lengths[i] = throttle.lengths[i - 1] * (i + 1);
    }
    if ((clp = conf_find_list("stages", clp, 1)) != NULL) {
        /* try reading their list. */
        s = NULL;
        i = -1;
        while ((s = conf_find_entry_next("", s, clp, 1)) != NULL) {
            i++;
            if (i >= throttle.stages)
                break; /* too many stages.. */
            if ((throttle.lengths[i] = str_conv_time(s, 0)) == 0) {
                if (i == 0)
                    throttle.lengths[i] = 180;
                else
                    throttle.lengths[i] = throttle.lengths[i - 1] * (i + 1);
            }
        }
    }

    return NULL;
}

HOOK_FUNCTION(throttle_timer_hook) {
    throttle_t *tp = (throttle_t *)data;

    tp->timer = TIMER_INVALID;
    if (me.now - tp->banned <= throttle.lengths[tp->stage]) {
        /* I'm not sure if this will happen.. */
        log_debug("throttle being expired when it shouldn't be?");
        tp->timer = create_timer(0, throttle.lengths[tp->stage],
                throttle_timer_hook, tp);
    } else
        destroy_throttle(tp);

    return NULL;
}

MODULE_LOADER(throttle) {

    memset(&throttle, 0, sizeof(throttle));
    throttle.table = create_hash_table(128,
            offsetof(struct throttle, addr), IPADDR_SIZE, 0, NULL);
    LIST_ALLOC(throttle.list);

    add_hook_before(ircd.events.stage1_connect, throttle_stage1_hook,
            acl_stage1_hook);
    add_hook(me.events.read_conf, throttle_conf_hook);

    throttle_conf_hook(NULL, NULL);

    return 1;
}
MODULE_UNLOADER(throttle) {

    /* just wipe them out.. */
    while (!LIST_EMPTY(throttle.list))
        destroy_throttle(LIST_FIRST(throttle.list));
    destroy_hash_table(throttle.table);
    LIST_FREE(throttle.list);

    remove_hook(ircd.events.stage1_connect, throttle_stage1_hook);
    remove_hook(me.events.read_conf, throttle_conf_hook);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
