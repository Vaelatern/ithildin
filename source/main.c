/*
 * main.c: the main() function and friends
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * "In My Egotistical Opinion, most people's C programs should be indented 
 * six feet downward and covered with dirt."  -- Blair P. Houghton
 */

#include <ithildin/stand.h>
#ifdef HAVE_OPENSSL
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

IDSTRING(rcsid, "$Id: main.c 832 2009-02-22 00:50:59Z wd $");

/* depending on the platform we might need to declare _malloc_options.  We
 * don't need to do this for FreeBSD since it's declared in stdlib.h for us
 * (handy) but we do for any other systems (I guess?) */
#ifndef __FreeBSD__
# ifdef USE_INTERNAL_MALLOC
extern
# endif
const char *_malloc_options;
#endif

/*
 * the writhing internal mass of the main() function is here, as well as a
 * few other small things.
 */

struct me_t me;

static void parse_args(int argc, char **argv);
HOOK_FUNCTION(stdout_log);
void sighandler_generic(int sig);
void sighandler_term(int sig);
#define SIG_ADD_HANDLER(func, signal) do {                                \
    struct sigaction sa;                                                  \
    sa.sa_handler = func;                                                 \
    sa.sa_flags = 0;                                                      \
    sigemptyset(&sa.sa_mask);                                             \
    sigaddset(&sa.sa_mask, signal);                                       \
    sigaction(signal, &sa, NULL);                                         \
} while(0);

#ifdef HAVE_OPENSSL
static void init_ssl(void);
#endif
#if defined(HAVE_GETRLIMIT) && defined(HAVE_SETRLIMIT)
static void change_rlimit(const char *, int, rlim_t);
#endif

static bool change_privileges(void);

int main(int argc, char **argv) {
    char currdir[PATH_MAX];
    char *s;
    time_t next;
        
#ifdef DEBUG_CODE
    _malloc_options = "ASJ";
#endif

    /* initialize ourselves, fill out the 'me' structure as well as possbile */
    memset(&me, 0, sizeof(struct me_t));

    /* Look at argv[0].  For the purposes of the program we want only the
     * program's name.  If argv[0] contains a full path name, we truncate it.
     * If it begins with a - we skip that. */
    if ((me.execname = strrchr(argv[0], '/')) == NULL)
        me.execname = argv[0];
    else
        me.execname += 1; /* skip the '/' */
    if (*me.execname == '-')
        me.execname = me.execname + 1;

    me.started = me.now = time(NULL);
    strncpy(me.conf_path, CONF_PATH, PATH_MAX);
    strncpy(me.lib_path, LIB_PATH, PATH_MAX);
    strncpy(me.data_path, DATA_PATH, PATH_MAX);
    sprintf(me.conf_file, "%s.conf", me.execname);
    sprintf(me.version, "%s-%i.%i.%i", BASENAME_VER, MAJOR_VER, MINOR_VER,
            PATCH_VER);
    me.revision = REPOVER;
    me.fork = 1; /* by default we fork */

    /* initialize the hook system before we setup our hooks */
    init_hooksystem();

    /* set up our static events */
    me.events.log_debug = create_event(EVENT_FL_NORETURN);
    me.events.log_notice = create_event(EVENT_FL_NORETURN);
    me.events.log_warn = create_event(EVENT_FL_NORETURN);
    me.events.log_error = create_event(EVENT_FL_NORETURN);
    me.events.log_unknown = create_event(EVENT_FL_NORETURN);
    me.events.read_conf = create_event(EVENT_FL_NORETURN);
    me.events.load_module = create_event(EVENT_FL_NORETURN);
    me.events.unload_module = create_event(EVENT_FL_NORETURN);
    me.events.afterpoll = create_event(EVENT_FL_NORETURN);
    me.events.shutdown = create_event(EVENT_FL_NORETURN);
    me.events.sighup = create_event(EVENT_FL_NORETURN);
    me.events.sigint = create_event(EVENT_FL_NORETURN);
    me.events.sigterm = create_event(EVENT_FL_NORETURN);
    me.events.sigusr1 = create_event(EVENT_FL_NORETURN);
    me.events.sigusr2 = create_event(EVENT_FL_NORETURN);

    /* immediately register the reload_conf function with sighup */
    add_hook(me.events.sighup, reload_conf);
        
    /* parse our command line arguments */
    parse_args(argc, argv);

    /* if the user has chosen to fork, install hook events for log output
     * anyways (but they will be removed when forking occurs */
    add_hook(me.events.log_notice, stdout_log);
    add_hook(me.events.log_warn, stdout_log);
    add_hook(me.events.log_error, stdout_log);
    add_hook(me.events.log_unknown, stdout_log);
    if (me.debug)
        add_hook(me.events.log_debug, stdout_log);

    if (!strcmp(me.execname, "chkconf")) {
        if (me.debug == false)
            add_hook(me.events.log_debug, stdout_log);
        me.debug = true;
        log_notice("checking configuration file %s/%s", me.conf_path,
                me.conf_file);
    } else
        log_notice("starting %s ...", me.version);

    /* now read in our conf file ... */
    log_debug("reading conf file from %s...", me.conf_file);
    getcwd(currdir, PATH_MAX);
    if (chdir(me.conf_path)) {
        log_error("couldn't chdir to conf directory %s", me.conf_path);
            return 1;
    }
    me.confhead = read_conf(me.conf_file);
    if (me.confhead == NULL) {
        log_error("couldn't parse configuration file %s/%s", me.conf_path,
                me.conf_file);
        return 1;
    }
    chdir(currdir);
    if ((s = conf_find_entry("directory", me.confhead, 1)) != NULL) {
        if (chdir(s) != 0) {
            log_error("couldn't change to directory %s: %s", s,
                    strerror(errno));
            return 1;
        }
    }
    if (!strcmp(me.execname, "chkconf")) {
        if (me.debug > 1)
            conf_display_tree(0, me.confhead);
        log_notice("configuration file %s/%s and includes are okay!",
                me.conf_path, me.conf_file);
        exit(0);
    }

#ifdef HAVE_OPENSSL
    init_ssl();
    log_notice("SSL support is %s", (me.ssl.enabled ? "enabled" : "disabled"));
#endif

#if defined(HAVE_GETRLIMIT) && defined(HAVE_SETRLIMIT)
    /* unlimit ourselves in useful fashion */
# ifdef RLIMIT_CORE
    change_rlimit("core file size", RLIMIT_CORE, RLIM_INFINITY);
# endif
# ifdef RLIMIT_NOFILE
    change_rlimit("open file descriptors", RLIMIT_NOFILE, 0);
# endif
#endif

    /* now drop into the background (yippy-skippy) */
    if (!me.fork) {
        log_notice("not forking at user request");
        log_notice("process id is %d", getpid());
    }
    else {
        pid_t pid;
        if ((pid = fork()) != 0) {
            if (pid == -1) {
                log_error("could not fork into background! PROCESS NOT "
                        "STARTED!\n");
                return 1;
            } else {
                log_notice("forked to pid %d, server finishing bootup now...",
                        pid);
                return 0;
            }
        } else {
            /* this has been added to give the debugger time to attach to the
             * newly spawned process.  if this inconveniences you, one might
             * ask why you're running -d anyways? */
            if (me.debug)
                sleep(30); /* snooze for 30 seconds */
        }
    }
    /* install signal handlers here */
    SIG_ADD_HANDLER(SIG_IGN, SIGPIPE);
    SIG_ADD_HANDLER(SIG_IGN, SIGALRM);
    SIG_ADD_HANDLER(sighandler_generic, SIGHUP);
    SIG_ADD_HANDLER(sighandler_generic, SIGINT);
    SIG_ADD_HANDLER(sighandler_generic, SIGUSR1);
    SIG_ADD_HANDLER(sighandler_generic, SIGUSR2);
    SIG_ADD_HANDLER(sighandler_term, SIGTERM);
        
    /* initialize the socket system (there are control variables in the
     * config files, which is why we do it here */
    init_socketsystem();
    /* build our modules list and do any auto-loading requested */
    build_module_list();
    /* now drop privileges */
    if (!change_privileges())
        return 1;

    /* trounce those stdout log bits (we do it here and not earlier in case
     * there is any important output from the socket/module init functions */
    if (me.fork) {
        remove_hook(me.events.log_notice, stdout_log);
        remove_hook(me.events.log_warn, stdout_log);
        remove_hook(me.events.log_error, stdout_log);
        remove_hook(me.events.log_unknown, stdout_log);
        if (me.debug)
            remove_hook(me.events.log_debug, stdout_log);
    }

    /* loop until poll_sockets returns 0 */
    next = 1; /* fuh */
    while (!me.shutdown && poll_sockets(next)) {
        reap_dead_sockets();
        next = exec_timers();
        hook_event(me.events.afterpoll, NULL);
        if (me.reloads) {
            do_module_reloads();
            me.reloads = 0;
        }
    }
        
    if (me.shutdown) {
        hook_event(me.events.shutdown, NULL);
        /* exit_process will exit for us. */
        exit_process(NULL, NULL);
    }

    log_error("going down in flames...");
    exit(1); /* this is a bad exit...  this should really never happen */
}

static const char *usage = "\
usage: %s [-Cdhmv] [-c file] [-l path] [-p path] [file]\n\
      -c <file>    loads the specified configuration file\n\
      -C           invoke in configuration checking-mode; the server does\n\
                   not start, but simply checks the syntax of the config\n\
                   files (-p and -c also work)\n\
      -d           turns on debugging (log_debug type messages are logged)\n\
      -h           displays this help data\n\
      -l <path>    changes the default library path to the specified one\n\
      -n           prevents the daemon from forking (and logs to stdout)\n\
      -p <path>    changes the default configuration path to the specified one\n\
      -v           displays pertinent version information\n\
";

void parse_args(int argc, char **argv) {
    char c;

    while ((c = getopt(argc, argv, "c:Cdhl:np:v")) != -1) {
        switch (c) {
            case 'c':
                if (strrchr(optarg, '/') != NULL)
                    snprintf(me.conf_file, PATH_MAX, "%s",
                            strrchr(optarg, '/') + 1);
                else
                    strncpy(me.conf_file, optarg, PATH_MAX);
                break;
            case 'C':
                /* dirty hack */
                me.execname = "chkconf";
                break;
            case 'd':
                me.debug++;
                break;
            case 'h':
                printf(usage, me.execname);
                exit(0);
            case 'l':
                strncpy(me.lib_path, optarg, PATH_MAX);
                break;
            case 'n':
                me.fork = 0;
                add_hook(me.events.sigint, exit_process);
                break;
            case 'p':
                strncpy(me.conf_path, optarg, PATH_MAX);
                break;
            case 'v':
                printf("%s (r%d)\n", me.version, me.revision);
                printf("CFLAGS=%s\nCFLAGSDLL=%s\nLDFLAGS=%s\nLDFLAGSDLL=%s\n",
                        COMP_FLAGS, COMP_FLAGS_MOD, COMP_LDFLAGS,
                        COMP_LDFLAGS_MOD);
                printf("CONF_PATH=%s\nLIB_PATH=%s\nDATA_PATH=%s\n", CONF_PATH,
                        LIB_PATH, DATA_PATH);
                exit(0);
            case '?':
                printf(usage, me.execname);
                exit(1);
        }
    }

    /* see if the conf file is named on the command line */
    if (argc > optind) {
        if (strrchr(argv[optind], '/') != NULL)
            snprintf(me.conf_file, PATH_MAX, "%s",
                    strrchr(argv[optind], '/') + 1);
        else
            strncpy(me.conf_file, argv[optind], PATH_MAX);
    }
}

HOOK_FUNCTION(stdout_log) {
    struct log_event_data *ldp = (struct log_event_data *)data;

    printf("%8s: %s%s%s\n", log_conv_str(ldp->level), ldp->module,
            (*ldp->module != '\0' ? ": " : ""), ldp->msg);
    return NULL;
}

HOOK_FUNCTION(reload_conf) {
    char currdir[PATH_MAX];
    conf_list_t *newconf = NULL;
    conf_list_t *oldconf = NULL;

    getcwd(currdir, PATH_MAX);
    if (chdir(me.conf_path)) {
        log_error("couldn't chdir to conf directory %s while reloading conf ",
                me.conf_path);
        return NULL;
    }
    newconf = read_conf(me.conf_file);
    if (newconf == NULL) {
        log_error("couldn't read configuration file %s while reloading conf!",
                me.conf_file);
        return NULL;
    }
    chdir(currdir);

    oldconf = me.confhead;
    me.confhead = newconf;
    build_module_list(); /* rebuild the module list */
    hook_event(me.events.read_conf, (void *)me.confhead);
    destroy_conf_branch(oldconf);
    log_notice("reloaded configuration file %s/%s", me.conf_path,
            me.conf_file);

    return NULL;
}

/* eventually aborting existence should consist of more cleanup, right now
 * all we do is destroy all our sockets.  probably what I should do is write
 * a *_shutdown function for each subsystem.  later.  */
HOOK_FUNCTION(exit_process) {
    struct isocket *sp;

    /* if this function is called pre-shutdown, simply set the shutdown flag,
     * we will get called again later to do cleanup. */
    if (me.shutdown == 0) {
        me.shutdown = -1;
        return NULL;
    }

#ifdef USE_DMALLOC
    dmalloc_log_stats();
    dmalloc_log_unfreed();
#endif

    /* unload all our modules */
    unload_all_modules();

    /* free our conf stuff */
    destroy_conf_branch(me.confhead);

    /* thrash all our sockets */
    reap_dead_sockets();
    LIST_FOREACH(sp, &allsockets, intlp) {
        destroy_socket(sp);
    }
    reap_dead_sockets();

#ifdef HAVE_OPENSSL
    if (me.ssl.enabled)
        ERR_free_strings();
#endif

#ifdef USE_DMALLOC
    dmalloc_log_stats();
    dmalloc_log_unfreed();
#endif

    exit(0);
    return NULL; /* NOTREACHED */
}

void sighandler_generic(int sig) {
    switch (sig) {
        case SIGHUP:
            log_notice("signal SIGHUP received");
            hook_event(me.events.sighup, NULL);
            break;
        case SIGINT:
            log_notice("signal SIGINT received");
            hook_event(me.events.sigint, NULL);
            break;
        case SIGUSR1:
            log_notice("signal SIGUSR1 received");
            hook_event(me.events.sigusr1, NULL);
            break;
        case SIGUSR2:
            log_notice("signal SIGUSR2 received");
            hook_event(me.events.sigusr2, NULL);
            break;
        default:
            log_warn("sighandler_generic called with unknown signal %d", sig);
            break;
    }
    return;
}

void sighandler_term(int sig) {
    switch (sig) {
        case SIGTERM:
            log_notice("signal SIGTERM received");
            hook_event(me.events.sigterm, NULL);
            break;
        default:
            log_warn("sighandler_term called with unknown signal %d", sig);
            break;
    }
    me.shutdown = SIGTERM; /* shutting down */
}

#ifdef HAVE_OPENSSL
/* This function initializes the non-socket parts of SSL.  If SSL support is
 * not compiled in, it simply sets me.have_ssl to 0 and returns.  Otherwise it
 * looks for proper configuration data to enable SSL in the daemon. */
static void init_ssl(void) {
    conf_list_t *clp;
    char *s;
    size_t ebytes = 128;

    /* First check to see if they configured SSL at all.  Not configuring it is
     * the equivalent of runtime disabling. */
    if ((clp = conf_find_list("ssl", me.confhead, 1)) == NULL) {
        log_notice("no ssl configuration section found.");
        return;
    }

    /* Now initialize the SSL library */
    if (!SSL_library_init()) {
        log_error("could not initialize OpenSSL library.");
        exit(1);
    }
    SSL_load_error_strings();

    /* Now try to seed the SSL PRNG. */
    if ((s = conf_find_entry("entropy-bits", clp, 1)) != NULL)
        ebytes = str_conv_int(s, 1024) / 8;
    if ((s = conf_find_entry("entropy", clp, 1)) == NULL) {
        log_warn("no entropy source defined in the ssl section.");
        return;
    }
    if (!strncmp(s, "egd:", 4)) {
        s += 4;
        if ((ebytes = RAND_egd_bytes(s, ebytes)) <= 0) {
            log_error("could not seed the OpenSSL PRNG using EGD socket %s: "
                    "%s", s, ERR_error_string(ERR_get_error(), NULL));
            return;
        } else
            log_debug("egd socket on %s yielded %d bits of entropy", s,
                    ebytes * 8);
    } else {
        if ((ebytes = RAND_load_file(s, ebytes)) <= 0) {
            log_error("could not seed the OpenSSL PRNG using random data "
                    "from %s: %s", s, ERR_error_string(ERR_get_error(), NULL));
            return;
        } else
            log_debug("entropy file %s yielded %d bits of entropy", s,
                    ebytes * 8);
    }

    /* And lastly, check to see that we have at least key/cert files.  also
     * look for an (optional) CA file.  XXX: at present we do not support CA
     * directories.  If this is desirable support will be added. */
    if ((s = conf_find_entry("certificate-file", clp, 1)) == NULL) {
        log_error("certificate file not defined.  ssl will be disabled.");
        return;
    }
    if (*s == '/')
        strlcpy(me.ssl.certfile, s, PATH_MAX);
    else
        snprintf(me.ssl.certfile, PATH_MAX, "%s/%s", me.conf_path, s);
    if (access(me.ssl.certfile, R_OK) != 0) {
        log_error("could not access certificate file %s: %s", me.ssl.certfile,
                strerror(errno));
        return;
    }

    if ((s = conf_find_entry("key-file", clp, 1)) == NULL) {
        log_error("private key file not defined.  ssl will be disabled.");
        return;
    }
    if (*s == '/')
        strlcpy(me.ssl.keyfile, s, PATH_MAX);
    else
        snprintf(me.ssl.keyfile, PATH_MAX, "%s/%s", me.conf_path, s);
    if (access(me.ssl.keyfile, R_OK) != 0) {
        log_error("could not access key file %s: %s", me.ssl.keyfile,
                strerror(errno));
        return;
    }

    /* the CA file is optional, but if it defined it must be accessible. */
    if ((s = conf_find_entry("ca-file", clp, 1)) != NULL) {
        if (*s == '/')
            strlcpy(me.ssl.cafile, s, PATH_MAX);
        else
            snprintf(me.ssl.cafile, PATH_MAX, "%s/%s", me.conf_path, s);
        if (access(me.ssl.cafile, R_OK) != 0) {
            log_error("could not access CA file %s: %s", me.ssl.cafile,
                    strerror(errno));
            return;
        }
    }

    me.ssl.verify = str_conv_bool(
            conf_find_entry("verify-certificates", clp, 1), 1);
    me.ssl.hs_timeout = str_conv_time(
            conf_find_entry("handshake-timeout", clp, 1), 30);

    me.ssl.enabled = true; /* enable SSL. */
}
#endif

#if defined(HAVE_GETRLIMIT) && defined(HAVE_SETRLIMIT)
/* rlimit-changer, with cute output. */
static void change_rlimit(const char *name, int var, rlim_t value) {
    struct rlimit rl;

    if (getrlimit(var, &rl) == 0) {
        if (value == 0)
            value = rl.rlim_max;
        rl.rlim_cur = value;
        if (setrlimit(var, &rl) == 0) {
            if (rl.rlim_cur == RLIM_INFINITY)
                log_notice("Resource limit for %s set to infinity.", name);
            else
                log_notice("Resource limit for %s set to %lld", name,
                        (int64_t)rl.rlim_cur);
        } else {
            if (value == RLIM_INFINITY) {
                getrlimit(var, &rl); /* in case rl got trashed.. */
                rl.rlim_cur = rl.rlim_max;
                if (setrlimit(var, &rl) == 0) {
                    log_debug("Failed to set resource limit for %s to "
                            "infinity.", name);
                    log_notice("Resource limit for %s set to %lld", name,
                            (int64_t)rl.rlim_cur);
                    return;
                }
            }
            log_notice("Failed to change resource limit for %s, currently "
                    "%lld", name, (int64_t)rl.rlim_cur);
        }
    }
}
#endif

/* This is the functionality for dropping privileges at run-time. */
#ifdef HAVE_GRP_H
#include <grp.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
static bool change_privileges(void) {
    char *s;
    bool drop_all = str_conv_bool(conf_find_entry("drop-privileges",
                me.confhead, 1), true);

#if defined(HAVE_GETGRNAM) && defined(HAVE_GETGID) && defined(HAVE_GETUID) \
    && defined(HAVE_SETGID)
# ifndef NGROUPS_MAX
# define NGROUPS_MAX 32
# endif
    if (getuid() == 0 &&
            (s = conf_find_entry("groups", me.confhead, 1)) != NULL) {
        struct group *grp;
        gid_t gids[NGROUPS_MAX];
        int cnt = 0;
        char *cur;

        while ((cur = strsep(&s, ",\t ")) != NULL) {
            if (*cur == '\0')
                continue;
            if ((grp = getgrnam(cur)) != NULL) {
                if (cnt == 0 && !drop_all) {
                    /* if this is our first group and drop_all is not set ten
                     * call setegid() */
# if defined(HAVE_GETEGID) && defined(HAVE_SETEGID)
                    setegid(grp->gr_gid);
                    if (getegid() != grp->gr_gid) {
                        log_error("could not change effective gid to %s (%d)",
                                gr->gr_name, gr->gid);
                        return false;
                    }
# endif
                }
                gids[cnt++] = grp->gr_gid;

# ifndef HAVE_SETGROUPS
#  ifdef HAVE_SETEGID
                /* only make this conditional if we could change the egid
                 * above. */
                if (drop_all)
#  endif
                {
                    setgid(grp->gr_gid);
                    if (getgid() != grp->gr_gid) {
                        log_error("could not change to group %s (%d)",
                                grp->gr_name, grp->gr_gid);
                        return false;
                    }
                }
                break;
# endif
            } else {
                log_error("could not find group %s", cur);
                return false;
            }
        }

# ifdef HAVE_SETGROUPS
        if (setgroups(cnt, gids) != 0) {
            log_error("could not change group list: %s", strerror(errno));
            return false;
        }
# endif
    }
#endif

#if defined(HAVE_GETPWNAM) && defined(HAVE_GETUID)
    if (getuid() == 0 &&
            (s = conf_find_entry("username", me.confhead, 1)) != NULL) {
        struct passwd *pwd = getpwnam(s);

        if (pwd != NULL) {
            if (drop_all) {
                if (setuid(pwd->pw_uid) != 0) {
                    log_error("could not change uid: %s", strerror(errno));
                    return false;
                }
                log_notice("changed effective and real uid to %s (%d)",
                        pwd->pw_name, pwd->pw_uid);
            } else {
                /* only change the effective uid. */
# if defined(HAVE_GETEUID) && defined(HAVE_SETEUID)
                if (seteuid(pwd->pw_uid) != 0) {
                    log_error("could not euid: %s", strerror(errno));
                    return false;
                }
                log_notice("changed effective uid to %s (%d)", pwd->pw_name,
                        pwd->pw_uid);
# endif
            }
        } else {
            log_error("could not find user %s", s);
            return false;
        }
    }
#endif

    return true;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
