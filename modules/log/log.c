/*
 * log.c: the log module
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * The log module is a more sophisticated version of the syslog module.  It
 * allows output from the log events to be sorted based on module and output to
 * files as well as syslog.  It also allows the user to define the output
 * format for files.
 */

#include <syslog.h>
#include <ithildin/stand.h>

IDSTRING(rcsid, "$Id: log.c 763 2006-07-14 01:16:04Z wd $");

MODULE_REGISTER("1.0");

/* don't forget to tag our own generated messages :) */
#undef LOG_MODULENAME
#define LOG_MODULENAME "log"

/* this structure contains an individual 'hook' for a log event.  it holds the
 * criteria used for matching as well as references for what to do upon a
 * match. */
typedef struct log_hook {
    char    *module;            /* module name pattern */
    char    *msg;               /* message pattern */
    enum logtypes level;        /* level to match at (or 0) */

#define LOG_FL_DEFUNCT  0x01    /* defunct log rule (some non-recoverable error
                                   occured when previously using this rule) */
#define LOG_FL_PASS     0x02    /* pass-through.  do not stop at this rule */
#define LOG_FL_IGNORE   0x04    /* ignore log messages which match this */
#define LOG_FL_SYSLOG   0x10    /* log to syslog */
#define LOG_FL_FILE     0x20    /* log to a file */
    int     flags;
    char    filename[PATH_MAX]; /* name of the file we will log to */
    FILE    *file;              /* file to log to (NULL is syslog) */
    int     prio;               /* priority (syslog only) */
    time_t  last;               /* last time we logged .. */
    time_t  rotate;             /* time to rotate (0 is never) */
    char    *ts_fmt;            /* timestamp formatting */

    TAILQ_ENTRY(log_hook) lp;
} log_hook_t;

/* some default configs we look for */
#define LOG_TS_FMT "[%Y-%m-%d %H:%M:%S]"
#define LOG_FILE_FMT "%Y-%m-%d.%H%M"
static time_t default_rotate;
static char *default_ts_fmt;
static char default_log_dir[PATH_MAX];

static TAILQ_HEAD(, log_hook) log_hook_list;
static conf_list_t **log_confdata;

static HOOK_FUNCTION(log_log_hook);
static HOOK_FUNCTION(log_reload_hook);

static int log_parse_conf(conf_list_t *);
static log_hook_t *log_hook_create(void);
static void log_hook_destroy(log_hook_t *);

/* this is the actual executor function.  it takes a log entry and scans
 * through each log hook until it finds one that matches.   when one matches it
 * logs in the appropriate direction and stops unless that match has the pass
 * bit set.  pretty simple. :) */
static HOOK_FUNCTION(log_log_hook) {
    struct log_event_data *ldp = (struct log_event_data *)data;
    log_hook_t *lhp;
    time_t last;

    TAILQ_FOREACH(lhp, &log_hook_list, lp) {
        if (lhp->flags & LOG_FL_DEFUNCT)
            continue; /* skip this entry */

        if ((lhp->module == NULL || match(lhp->module, ldp->module)) &&
                (lhp->msg == NULL || match(lhp->msg, ldp->msg)) &&
                (lhp->level == 0 || lhp->level == ldp->level)) {
            /* a match! */

            last = lhp->last;
            lhp->last = me.now;

            if (lhp->flags & LOG_FL_IGNORE)
                return NULL; /* nothing to do now */

            if (lhp->flags & LOG_FL_FILE) {
                char fname[PATH_MAX];
                char timestr[PATH_MAX];
                struct tm *tmp;

                tmp = localtime(&me.now);
                /* Check to see if rotation must occur.. */
                *fname = '\0';
                if (lhp->rotate &&
                    (last / lhp->rotate != lhp->last / lhp->rotate)) {
                    time_t rtime;
                    
                    /* we need to rotate.. */
                    if (lhp->file != NULL) {
                        fclose(lhp->file);
                        lhp->file = NULL;
                    }

                    /* let us modify tmp by giving it the time with the
                     * excessive goo shaved off so that we get filenames
                     * with ts rounded to when they 'should' have been
                     * opened. */
                    rtime = lhp->last - (lhp->last % lhp->rotate);
                    tmp = localtime(&rtime);
                    
                    strftime(timestr, PATH_MAX, LOG_FILE_FMT, tmp);
                    snprintf(fname, PATH_MAX, "%s.%s", lhp->filename,
                            timestr);
                }
                
                if (lhp->file == NULL) {
                    if (*fname == '\0')
                        strcpy(fname, lhp->filename);

                    if ((lhp->file = fopen(fname, "a")) == NULL) {
                        /* Be sure to set DEFUNCT *first* to avoid nasty log
                         * recursion!  This way when we come back around to
                         * this rule we will not hit it again. */
                        lhp->flags |= LOG_FL_DEFUNCT;
                        log_error("cannot open logfile %s: %s",
                                lhp->filename, strerror(errno));
                    }
                }

                if (lhp->file != NULL) {
                    tmp = localtime(&lhp->last);
                    strftime(timestr, PATH_MAX, lhp->ts_fmt, tmp);
                    if (fprintf(lhp->file, "%s %s: %s%s%s\n",
                            timestr, log_conv_str(ldp->level),
                            ldp->module, (*ldp->module != '\0' ? ": " : ""),
                            ldp->msg) < 0) {
                    lhp->flags |= LOG_FL_DEFUNCT;
                    fclose(lhp->file);
                    lhp->file = NULL;
                    /* XXX: might be nice to give more info.. :/ */
                    log_error("an error occured while doing file logging: %s",
                            strerror(errno));
                    } else
                        fflush(lhp->file);
                }
            }
            if (lhp->flags & LOG_FL_SYSLOG) {
                /* we need to deduce the priority if it wasn't specified */
                int prio;

                if ((prio = lhp->prio) == 0) {
                    switch (ldp->level) {
                        case LOGTYPE_DEBUG:
                            prio = LOG_DEBUG;
                            break;
                        case LOGTYPE_WARN:
                            prio = LOG_WARNING;
                            break;
                        case LOGTYPE_ERROR:
                            prio = LOG_ERR;
                            break;
                        default:
                        case LOGTYPE_NOTICE:
                            prio = LOG_INFO;
                            break;
                    }
                }

                syslog(prio, "%s%s%s", ldp->module,
                        (*ldp->module != '\0' ? ": " : ""), ldp->msg);

            }

            if ((lhp->flags & LOG_FL_PASS) == 0)
                return NULL;
        }
    }

    return NULL;
}

static HOOK_FUNCTION(log_reload_hook) {

    if (!log_parse_conf(*log_confdata))
        log_warn("a problem occured while parsing the configuration data for "
                "the log module.  if you can read this you're lucky ;)");

    return NULL;
}

static int log_parse_conf(conf_list_t *conf) {
    char *s;
    int facility = LOG_DAEMON;
    conf_list_t *clp = NULL;
    log_hook_t *lhp;

    /* nuke the current list of hooks */
    while (!TAILQ_EMPTY(&log_hook_list))
        log_hook_destroy(TAILQ_FIRST(&log_hook_list));

    /* see if conf is NULL.  if it is we're basically just a syslog module */
    if (conf == NULL) {
        openlog(BASENAME_VER, LOG_PID, facility);
        /* Make a hook that dumps to syslog. */
        lhp = log_hook_create();
        lhp->flags |= LOG_FL_SYSLOG;

        return 1;
    }

    /* now look for configuration directives.. start with the one-offs. */
    if ((s = conf_find_entry("directory", conf, 1)) != NULL)
        snprintf(default_log_dir, PATH_MAX, "%s/", s);
    else
        strcpy(default_log_dir, "");

    if ((s = conf_find_entry("syslog-facility", conf, 1)) != NULL) {
        if (!strcasecmp(s, "daemon"))
            facility = LOG_DAEMON;
        else if (!strcasecmp(s, "user"))
            facility = LOG_USER;
        else if (!strncasecmp(s, "local", 5)) {
            switch (*(s + 5)) {
                case '1':
                    facility = LOG_LOCAL1;
                    break;
                case '2':
                    facility = LOG_LOCAL2;
                    break;
                case '3':
                    facility = LOG_LOCAL3;
                    break;
                case '4':
                    facility = LOG_LOCAL4;
                    break;
                case '5':
                    facility = LOG_LOCAL5;
                    break;
                case '6':
                    facility = LOG_LOCAL6;
                    break;
                case '7':
                    facility = LOG_LOCAL7;
                    break;
                case '0':
                default:
                    facility = LOG_LOCAL0;
                    break;
            }
        }
    }

    if ((s = conf_find_entry("timestamp-format", conf, 1)) != NULL) {
        if (default_ts_fmt != NULL)
            free(default_ts_fmt);
        default_ts_fmt = strdup(s);
    } else
        default_ts_fmt = strdup(LOG_TS_FMT);

    default_rotate = str_conv_time(conf_find_entry("rotate", conf, 1), 0);

    if ((s = conf_find_entry("syslog-identity", conf, 1)) == NULL)
        s = me.execname;
    openlog(s, LOG_PID, facility);

    /* okay, now parse the rules */
    while ((clp = conf_find_list_next("rule", clp, conf, 1)) != NULL) {
        lhp = log_hook_create();

        /* we might generate logging messages before this is done, make sure
         * this doesn't get hooked. :) */
        lhp->flags |= LOG_FL_DEFUNCT;

        if ((s = conf_find_entry("module", clp, 1)) != NULL)
            lhp->module = strdup(s);
        if ((s = conf_find_entry("message", clp, 1)) != NULL)
            lhp->msg = strdup(s);
        if ((s = conf_find_entry("level", clp, 1)) != NULL)
            lhp->level = str_conv_log(s);

        if (str_conv_bool(conf_find_entry("pass", clp, 1), false))
            lhp->flags |= LOG_FL_PASS;
        if (str_conv_bool(conf_find_entry("ignore", clp, 1), false))
            lhp->flags |= LOG_FL_IGNORE;
        if (str_conv_bool(conf_find_entry("syslog", clp, 1), false))
            lhp->flags |= LOG_FL_SYSLOG;

        if ((s = conf_find_entry("file", clp, 1)) != NULL) {
            lhp->flags |= LOG_FL_FILE;
            if (*default_log_dir == '\0' || *s == '/')
                /* if we have no default directory or s is an absolute path
                 * let's just copy it */
                strlcpy(lhp->filename, s, PATH_MAX);
            else
                snprintf(lhp->filename, PATH_MAX, "%s%s", default_log_dir, s);
        } else
            lhp->flags |= LOG_FL_SYSLOG;
        if ((s = conf_find_entry("priority", clp, 1)) != NULL) {
            if (!strncasecmp(s, "emergency", 5))
                lhp->prio = LOG_EMERG;
            else if (!strcasecmp(s, "alert"))
                lhp->prio = LOG_ALERT;
            else if (!strncasecmp(s, "critical", 4))
                lhp->prio = LOG_CRIT;
            else if (!strncasecmp(s, "error", 3))
                lhp->prio = LOG_ERR;
            else if (!strncasecmp(s, "warning", 4))
                lhp->prio = LOG_WARNING;
            else if (!strcasecmp(s, "notice"))
                lhp->prio = LOG_NOTICE;
            else if (!strcasecmp(s, "info"))
                lhp->prio = LOG_INFO;
            else if (!strcasecmp(s, "debug"))
                lhp->prio = LOG_DEBUG;
            else
                log_warn("unknown priority type %s", s);
        }

        lhp->last = 0;
        lhp->rotate = str_conv_time(conf_find_entry("rotate", clp, 1),
                default_rotate);
        if ((s = conf_find_entry("timestamp-format", clp, 1)) != NULL)
            lhp->ts_fmt = strdup(s);
        else
            lhp->ts_fmt = strdup(default_ts_fmt);

        /* excellent, all done.  remove any defunct status (in case we're
         * reloading the rules we want to do this. */
        lhp->flags &= ~LOG_FL_DEFUNCT;
    }

    return 1;
}

static log_hook_t *log_hook_create(void) {
    log_hook_t *lhp = malloc(sizeof(log_hook_t));

    memset(lhp, 0, sizeof(log_hook_t));
    
    if (TAILQ_EMPTY(&log_hook_list))
        TAILQ_INSERT_HEAD(&log_hook_list, lhp, lp);
    else
        TAILQ_INSERT_TAIL(&log_hook_list, lhp, lp);

    return lhp;
}

static void log_hook_destroy(log_hook_t *lhp) {

    TAILQ_REMOVE(&log_hook_list, lhp, lp);

    if (lhp->module != NULL)
        free(lhp->module);
    if (lhp->msg != NULL)
        free(lhp->msg);
    if (lhp->file != NULL)
        fclose(lhp->file);
    if (lhp->ts_fmt != NULL)
        free(lhp->ts_fmt);
    free(lhp);
}

MODULE_LOADER(log) {

    log_confdata = confdata;
    if (!log_parse_conf(*confdata))
        return 0;

    add_hook(me.events.read_conf, log_reload_hook);

    add_hook(me.events.log_debug, log_log_hook);
    add_hook(me.events.log_notice, log_log_hook);
    add_hook(me.events.log_warn, log_log_hook);
    add_hook(me.events.log_error, log_log_hook);
    add_hook(me.events.log_unknown, log_log_hook);

    return 1;
}

MODULE_UNLOADER(log) {

    while (!TAILQ_EMPTY(&log_hook_list))
        log_hook_destroy(TAILQ_FIRST(&log_hook_list));

    if (default_ts_fmt != NULL)
        free(default_ts_fmt);

    remove_hook(me.events.read_conf, log_reload_hook);

    remove_hook(me.events.log_debug, log_log_hook);
    remove_hook(me.events.log_notice, log_log_hook);
    remove_hook(me.events.log_warn, log_log_hook);
    remove_hook(me.events.log_error, log_log_hook);
    remove_hook(me.events.log_unknown, log_log_hook);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
