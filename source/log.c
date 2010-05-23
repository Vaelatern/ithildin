/*
 * log.c: logging support functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains a simple group of functions to log messages from the
 * daemon.  No actual I/O is done here, this is simply a set of mechanisms to
 * hook the appropriate events.
 */

#include <ithildin/stand.h>

IDSTRING(rcsid, "$Id: log.c 762 2006-07-14 01:11:45Z wd $");

/* convert a log type to a string describing what it is */
const char *log_conv_str(enum logtypes type) {

    switch (type) {
        case LOGTYPE_DEBUG:
            return "debug";
        case LOGTYPE_NOTICE:
            return "notice";
        case LOGTYPE_WARN:
            return "warning";
        case LOGTYPE_ERROR:
            return "error";
        default:
            return "unknown";
    }
}

/* try to convert a string specifying a logtype to the type itself */
enum logtypes str_conv_log(const char *str) {

    if (!strcasecmp(str, "debug"))
        return LOGTYPE_DEBUG;
    else if (!strcasecmp(str, "notice"))
        return LOGTYPE_NOTICE;
    else if (!strncasecmp(str, "warn", 4))
        return LOGTYPE_WARN;
    else if (!strncasecmp(str, "err", 3))
        return LOGTYPE_ERROR;
    else
        return LOGTYPE_UNKNOWN;
}

#ifdef NEED_INDIVIDUAL_LOG_FUNCTIONS
void log_debug(const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    log_vmsg(LOGTYPE_DEBUG, "", msg, ap);
    va_end(ap);
}

void log_notice(const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    log_vmsg(LOGTYPE_NOTICE, "", msg, ap);
    va_end(ap);
}

void log_warn(const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    log_vmsg(LOGTYPE_WARN, "", msg, ap);
    va_end(ap);
}

void log_error(const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    log_vmsg(LOGTYPE_ERROR, "", msg, ap);
    va_end(ap);
}
#endif

void log_msg(enum logtypes type, const char *mod, const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    log_vmsg(type, mod, msg, ap);
    va_end(ap);
}

/* Recursion protection for logged messages.  If we log_*() and then hook an
 * event which generates a log_*() we can end up in serious trouble. :) */
#define LOG_RECURSE_MAX 10
static uint32_t log_recursed;

void log_vmsg(enum logtypes type, const char *mod, const char *msg,
        va_list ap) {
    static char logmsg[LOG_MSG_MAXLEN];
    struct log_event_data led;

    if (!me.debug && type == LOGTYPE_DEBUG)
        return; /* avoid doing anything for debug messages when we don't
                   want to */

    log_recursed++;
    if (log_recursed == LOG_RECURSE_MAX) {
        log_recursed--;
        return;
    }

    vsnprintf(logmsg, LOG_MSG_MAXLEN, msg, ap);

    led.level = type;
    led.module = (mod != NULL ? mod : "");
    led.msg = logmsg;

    switch (type) {
        case LOGTYPE_DEBUG:
            hook_event(me.events.log_debug, (void *)&led);
            break;
        case LOGTYPE_NOTICE:
            hook_event(me.events.log_notice, (void *)&led);
            break;
        case LOGTYPE_WARN:
            hook_event(me.events.log_warn, (void *)&led);
            break;
        case LOGTYPE_ERROR:
            hook_event(me.events.log_error, (void *)&led);
            break;
        default:
            hook_event(me.events.log_unknown, (void *)&led);
            break;
    }

    log_recursed--;
}


/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
