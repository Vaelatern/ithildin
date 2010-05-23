/*
 * global.h: global structure definitions which don't fit anywhere else
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: global.h 578 2005-08-21 06:37:53Z wd $
 */

#ifndef GLOBAL_H
#define GLOBAL_H

/*
 * this is where any global variables will be declared, unless they are
 * of a special type, in which case they should be declared near their
 * appropriate type definition
 */

/* this is a structure defining parts of the process that would be worth
 * remembering.  it's a one off, statically allocated */

extern struct me_t {
    char    conf_path[PATH_MAX];/* path where data (conf files,
                                   motd, etc) live */
    char    conf_file[PATH_MAX];/* explicit name of conf file */
    char    lib_path[PATH_MAX];
    char    data_path[PATH_MAX];
        
    char    version[128];        /* a pre-allocated version string */
    int            revision;                /* revision number in the scm repo. */
    char    *execname;                /* the name of the executable (path data is
                                   stripped out) */

    time_t  started;                /* time of creation for the daemon */
    time_t  now;                /* the time now */

    int     debug;                /* are we in debugmode (logging extra
                                   data, etc) */
    bool    fork;                /* are we going to/have we forked */
    bool    shutdown;                /* are we going to shutdown? */
    bool    reloads;                /* do we have reloads pending? */
    bool    have_ssl;                /* is ssl available (compiled in) and
                                   configured? */
    
#ifdef HAVE_OPENSSL
    /* various ssl configuration items. */
    struct {
        bool enabled;                /* set if SSL support is enabled. */

        char certfile[PATH_MAX];/* certificate file */
        char keyfile[PATH_MAX];        /* key file */
        char cafile[PATH_MAX];        /* certificate authority file */
        /* This is the default SSL context object.  It contains liberal (in
         * terms of security) settings.  Callers which wish for more
         * restrictive settings should use their own SSL_CTX objects instead.
         * This object is suitable for making outbound SSL connections, and
         * also for accepting connections where key verification will not be
         * important. */
        struct ssl_ctx_st *ctx;
        bool verify;                /* set if we want to verify certificates
                                   (the default) */
        time_t hs_timeout;        /* the timeout for SSL handshakes. */
    } ssl;
#endif

    conf_list_t *confhead;        /* head of configuration data */

    LIST_HEAD(, module) modules;
    LIST_HEAD(, timer_event) timers;

    /* static events */
    struct {
        event_t *log_debug;        /* hooked for debug messages */
        event_t *log_notice;        /* notices */
        event_t *log_warn;        /* warnings */
        event_t *log_error;        /* errors */
        event_t *log_unknown;        /* and any other type */

        event_t *read_conf;        /* hooked when the conf is read */

        event_t *load_module;        /* hooked when a module is laoded */
        event_t *unload_module;        /* ... or unloaded */

        event_t *afterpoll;        /* called after each polling run */

        event_t *shutdown;        /* called just prior to shutdown */

        event_t *sighup;        /* called for SIGHUP .. */
        event_t *sigint;        /* ... SIGINT */
        event_t *sigterm;        /* ... SIGTERM */
        event_t *sigusr1;        /* ... SIGUSR1 */
        event_t *sigusr2;        /* ... SIGUSR2 */
    } events;
} me;

/* our conf reloading call. */
HOOK_FUNCTION(reload_conf);
/* our death function */
HOOK_FUNCTION(exit_process);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
