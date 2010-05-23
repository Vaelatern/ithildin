/*
 * log.h: logging prototypes
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: log.h 578 2005-08-21 06:37:53Z wd $
 */

#ifndef LOG_H
#define LOG_H

#define LOG_MSG_MAXLEN 16384
enum logtypes {
    LOGTYPE_DEBUG = 0,
    LOGTYPE_NOTICE = 1,
    LOGTYPE_WARN = 2,
    LOGTYPE_ERROR = 3,
    LOGTYPE_UNKNOWN = 255
};
const char *log_conv_str(enum logtypes);
enum logtypes str_conv_log(const char *);

/* this is the data structure passed through the various log_ events. */
struct log_event_data {
    enum logtypes level;
    const char *module;
    const char *msg;
};

/* these shouldn't actually be called by anyone, in theory. */
void log_msg(enum logtypes, const char *, const char *, ...) __PRINTF(3);
void log_vmsg(enum logtypes, const char *, const char *, va_list);

/* Redefine "LOG_MODULENAME" to change the source of log messages.  This is a
 * hackish way to allow at least modules with include files to add their names
 * to log messages by default. */
#define LOG_MODULENAME ""

/* For GNU C or C99 compilers just use macro varargs to replace the various
 * log_ functions. */
#if defined(__GNUC__) || __STDC_VERSION__ >= 199901L
# if defined(DEBUG_CODE)
#  define log_debug(msg, ...) log_msg(LOGTYPE_DEBUG, LOG_MODULENAME,        \
        msg, ## __VA_ARGS__)
# else
#  define log_debug(msg, ...) ((void)0)
# endif
# define log_notice(msg, ...) log_msg(LOGTYPE_NOTICE, LOG_MODULENAME,        \
        msg, ## __VA_ARGS__)
# define log_warn(msg, ...) log_msg(LOGTYPE_WARN, LOG_MODULENAME, msg,        \
        ## __VA_ARGS__)
# define log_error(msg, ...) log_msg(LOGTYPE_ERROR, LOG_MODULENAME, msg,\
        ## __VA_ARGS__)
#else
# define NEED_INDIVIDUAL_LOG_FUNCTIONS
void log_debug(const char *, ...) __PRINTF(1);
void log_notice(const char *, ...) __PRINTF(1);
void log_warn(const char *, ...) __PRINTF(1);
void log_error(const char *, ...) __PRINTF(1);
#endif

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
