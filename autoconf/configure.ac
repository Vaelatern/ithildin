dnl ------------------------------------------------------------------------
dnl configure.in: GNU autoconf script generator.
dnl
dnl Copyright 2002-2005 the Ithildin Project.
dnl See the COPYING file for more information on licensing and use.
AC_REVISION($Id: configure.ac 819 2008-09-25 05:21:02Z wd $)
dnl ------------------------------------------------------------------------
AC_INIT(ithildin, 1.1, wd@ithildin.org, ithildin)

AC_CONFIG_AUX_DIR(autoconf)
AC_CONFIG_HEADER(include/config.h)
AC_PREFIX_DEFAULT(/usr/local)

dnl ------------------------------------------------------------------------
dnl Get the version number/name/etc
dnl ------------------------------------------------------------------------
PACKAGE=ithildin

VERSION=`grep -E '^#define BASENAME_VER' include/ithildin/stand.h | sed s/'.define BASENAME_VER '// | sed s/'\"'//g | xargs echo -n`
VERSION="$VERSION-`grep -E '^#define MAJOR_VER' include/ithildin/stand.h | sed s/'.define MAJOR_VER '// | xargs echo -n`"
VERSION="$VERSION.`grep -E '^#define MINOR_VER' include/ithildin/stand.h | sed s/'.define MINOR_VER '// | xargs echo -n`"
VERSION="$VERSION.`grep -E '^#define PATCH_VER' include/ithildin/stand.h | sed s/'.define PATCH_VER '// | xargs echo -n`"

AC_SUBST(PACKAGE)
AC_SUBST(VERSION)

dnl ------------------------------------------------------------------------
dnl Check for machine/vendor/operating system type
dnl ------------------------------------------------------------------------
AC_CANONICAL_HOST

dnl ------------------------------------------------------------------------
dnl Checks for programs.
dnl ------------------------------------------------------------------------
AC_PROG_CC
ICC=no
AC_MSG_CHECKING([whether we are using the Intel C Compiler])
case `$CC -V 2>&1` in
    *Intel*)
        AC_MSG_RESULT([yes])
        ICC=yes
    ;;
    *)
        AC_MSG_RESULT([no])
    ;;
esac
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_CHECK_PROG(RM, rm, rm)

dnl ------------------------------------------------------------------------
dnl Checks for libraries.
dnl ------------------------------------------------------------------------
found_dlopen="yes"
AC_CHECK_LIB(dl, dlopen,, found_dlopen="no")
AC_CHECK_LIB(nsl, gethostbyname)
AC_CHECK_LIB(socket, socket)

dnl ------------------------------------------------------------------------
dnl Checks for header files.
dnl ------------------------------------------------------------------------
AC_HEADER_STDC
AC_CHECK_HEADERS(assert.h dl.h dlfcn.h errno.h fcntl.h features.h grp.h)
AC_CHECK_HEADERS(limits.h math.h netdb.h pwd.h unistd.h stdbool.h)
AC_CHECK_HEADERS(arpa/inet.h)
AC_CHECK_HEADERS(machine/endian.h)
AC_CHECK_HEADERS(netinet/in.h)
AC_CHECK_HEADERS(sys/mman.h sys/resource.h sys/socket.h sys/stat.h sys/time.h)
AC_HEADER_TIME

dnl ------------------------------------------------------------------------
dnl Checks for typedefs, structures, and compiler characteristics.
dnl ------------------------------------------------------------------------
AC_C_CONST
if test "$ac_cv_header_machine_endian_h" = "no" ; then
    AC_C_BIGENDIAN
fi
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(long long)
if test "$ac_cv_sizeof_short" = "2" ; then
    AC_CHECK_TYPE(int16_t, short)
    AC_CHECK_TYPE(uint16_t, unsigned short)
else
    if test "$ac_cv_sizeof_int" = "2" ; then
        AC_CHECK_TYPE(int16_t, int)
        AC_CHECK_TYPE(uint16_t, unsigned int)
    else
        AC_MSG_ERROR([I can't seem to find a 2 byte integer on your system!])
    fi
fi
if test "$ac_cv_sizeof_int" = "4" ; then
    AC_CHECK_TYPE(int32_t, int)
    AC_CHECK_TYPE(uint32_t, unsigned int)
else
    if test "$ac_cv_sizeof_long" = "4" ; then
        AC_CHECK_TYPE(int32_t, long)
        AC_CHECK_TYPE(uint32_t, unsigned long)
    else
        AC_MSG_ERROR([I can't seem to find a 4 byte integer on your system!])
    fi
fi
if test "$ac_cv_sizeof_long_long" = "8" ; then
    AC_CHECK_TYPE(int64_t, long long)
    AC_CHECK_TYPE(uint64_t, unsigned long long)
else
    if test "$ac_cv_sizeof_long" = "8" ; then
        AC_CHECK_TYPE(int64_t, long long)
        AC_CHECK_TYPE(uint64_t, unsigned long)
    else
        AC_MSG_ERROR([I can't seem to find an 8 byte integer on your system!])
    fi
fi
AC_C_LONG_DOUBLE
AC_C_INLINE

if test "$ac_cv_c_inline" = "no" ; then
    AC_MSG_ERROR([Your compiler has no inline support.])
fi

dnl ------------------------------------------------------------------------
dnl Checks for library functions.
dnl ------------------------------------------------------------------------
AC_FUNC_VPRINTF
AC_CHECK_FUNCS(strdup strerror strtod strtol strsep vsnprintf)
AC_CHECK_FUNCS(getcwd gettimeofday usleep)
if test "$found_dlopen" = "no" ; then
    AC_CHECK_FUNCS(dlopen)
fi

if test "$ac_cv_func_dlopen" = "no" ; then
    AC_MSG_ERROR([your system does not have dynamic library functions, you will be unable to use this software, sorry.])
fi

dnl -----
dnl look for 'fmtcheck' (on FreeBSD and NetBSD at least).  If it's not there
dnl use the contrib/ bits.  Also look for 'strlcpy' (a similar BSD call)
dnl -----
AC_CHECK_FUNCS(fmtcheck strlcat strlcpy)

dnl ----
dnl socket functions
dnl ----
AC_CHECK_FUNCS(kqueue poll select socket)
AC_CHECK_FUNCS(recv send setsockopt)

dnl ----
dnl stuff for malloc
dnl ----
AC_CHECK_FUNCS(mmap munmap madvise brk sbrk)
AC_CHECK_FUNCS(readlink getenv)

dnl ----
dnl some stuff for changing user ids
dnl ----
AC_CHECK_FUNCS(setuid seteuid setreuid setgid setegid setgroups)
AC_CHECK_FUNCS(geteuid getuid getgid getpwnam getgrnam)

dnl ----
dnl stuff to change coredump sizes
dnl ----
AC_CHECK_FUNCS(getrlimit setrlimit)

dnl ------------------------------------------------------------------------
dnl try to figure out the 'page shift' of the system by executing a test
dnl program which returns that count.  in our case a non-zero return
dnl actually means success
dnl ------------------------------------------------------------------------
AC_MSG_CHECKING([the system's memory page size])
pagesize="no"
AC_TRY_RUN([
#include <stdio.h>
#if HAVE_UNISTD_H
# include <unistd.h>
#endif

int main(void) {
    FILE *fp = fopen("conftest.malloc", "w");

    if (fp != NULL) {
        fprintf(fp, "%d\n", getpagesize());
        fclose(fp);
    } else
        exit(1);
    exit(0);
}],[
if test -f "conftest.malloc" ; then
    pagesize=`cat conftest.malloc`
fi
])
if test "$pagesize" != "no" ; then
    AC_MSG_RESULT($pagesize)
else
    if test "$ac_cv_sizeof_int" = "4" ; then
        pagesize=4096
    else
        pagesize=8192
    fi
    AC_MSG_RESULT([$pagesize (guessing)])
fi
AC_DEFINE_UNQUOTED(MALLOC_PAGESIZE, $pagesize,
    [the system's memory page size])

dnl ------------------------------------------------------------------------
dnl various malloc tests:
dnl - see if we can override the system malloc
dnl - see if we can use mmap and sbrk/brk
dnl ------------------------------------------------------------------------
AC_MSG_CHECKING([whether erobbins will hate me (override system malloc?)])
malloc_alternatives="no"
AC_TRY_RUN([
int okay = 0;
#define malloc test_malloc
char *test_malloc(int size) {
    okay = size;
    return (char *)0;
}
int main(void) {
    malloc(42);
    if (okay)
        exit(0);
    exit(1);
}],[malloc_override="yes"])
if test "$ac_cv_func_sbrk" = "yes" -a "$ac_cv_func_brk" = "yes" -a \
    "$ac_cv_func_mmap" = "yes" -a "$ac_cv_func_munmap" = yes ; then
    AC_DEFINE(MALLOC_LOWLEVEL, 1, [Really do low level malloc management])
    malloc_alternatives="yes"
fi

if test "$malloc_override" = "yes" -a "$malloc_alternatives" = "yes" ; then
    AC_DEFINE(MALLOC_OVERRIDE, 1, [Go ahead and override the system malloc])
    AC_MSG_RESULT([yes])
else
    AC_MSG_RESULT([no])
fi

dnl ------------------------------------------------------------------------
dnl Below are checks to determine how to do shared object support
dnl for making shared libraries and stuff like that.  This has been borrowed
dnl or based on various projects I've seen floating around.
dnl ------------------------------------------------------------------------
CCDLL="${CC}"
tldf=$LDFLAGS
AC_MSG_CHECKING(for the -export-dynamic ld flag)
LDFLAGS="${LDFLAGS} -Wl,-export-dynamic"
AC_TRY_LINK(, [int i;], found=yes, found=no)
AC_MSG_RESULT($found)
if test "$found" = "no" ; then
    LDFLAGS=$tldf
fi

if test -n "$GCC" ; then
    CFLAGSDLL="${CFLAGSDLL} -fPIC -DPIC -shared"
    else if test -n "$ICC" ; then
        CFLAGSDLL="${CFLAGSDLL} -KPIC -DPIC -shared"
    fi
fi

LDFLAGSDLL="-shared"

tldf=$LDFLAGS
AC_MSG_CHECKING(for the -nostdlib compiler flag)
LDFLAGS="${LDFLAGS} -nostdlib"
AC_TRY_LINK(, [int i;], found=yes, found=no)
AC_MSG_RESULT($found)
if test "$found" = "yes" ; then
    LDFLAGSDLL="$LDFLAGSDLL -nostdlib"
fi
LDFLAGS=$tldf

AC_SUBST(CCDLL)
AC_SUBST(CFLAGSDLL)
AC_SUBST(LDFLAGSDLL)
 
dnl -------------------------------------------------------------------
dnl Choose our type of socket handling.  the default is to use select()
dnl we add the --with argument first so the user can manually select,
dnl if desired.  in this case no checks as to the validity of their
dnl selection are made.
dnl -------------------------------------------------------------------
POLLER=""
AC_ARG_WITH(poller,
    AC_HELP_STRING([--with-poller=x],
        [set the poller to x (one of select, poll, kqueue, devpoll)]),
    POLLER="$with_poller"
    ithildin_cv_poller="$POLLER"
)

AC_CACHE_CHECK([for which polling function to use], ithildin_cv_poller,
[

if test "x$POLLER" = "x" ; then
    case "$host_os" in
        freebsd*|netbsd*|openbsd*)
            if test "$ac_cv_func_kqueue" = "yes" ; then
                POLLER=kqueue
            fi
        ;;
        solaris*|sunos*|linux*)
            if test -e /dev/poll ; then
                POLLER=/dev/poll
            fi
        ;;
    esac

    if test -z "$POLLER" ; then
        if test "$ac_cv_func_select" = "yes" ; then
            POLLER=select
        else
            if test "$ac_cv_func_poll" = "yes" ; then
                POLLER=poll
            else
                AC_MSG_ERROR(
        [your system doesn't seem to have select or poll, or any other
        method by which to check socket activity.  this daemon will not
        function, sorry.])
            fi
        fi
    fi
fi
ithildin_cv_poller="$POLLER"
])

POLLER="$ithildin_cv_poller"
case "$POLLER" in
    select)
        AC_DEFINE(POLLER_SELECT, 1, [The select() system call])
    ;;
    poll)
        AC_DEFINE(POLLER_POLL, 1, [The poll() system call])
    ;;
    kqueue)
        AC_DEFINE(POLLER_KQUEUE, 1, [The kqueue() system call])
    ;;
    devpoll)
        AC_DEFINE(POLLER_DEVPOLL, 1, [The /dev/poll device])
    ;;
    *)
        AC_MSG_ERROR([The poller "$POLLER" is not supported by this system,
try again?])
    ;;
esac
AC_SUBST(POLLER)

dnl ------------------------------------------------------------------------
dnl this function is used for pretty output generation.
dnl ------------------------------------------------------------------------
ith_feature_append () {
    if test -z "$extra_features" ; then
        extra_features="$*"
    else
        extra_features="${extra_features}, $*"
    fi
}
    
dnl ------------------------------------------------------------------------
dnl IPv6 checks
dnl ------------------------------------------------------------------------

dnl look for getaddrinfo and company.
AC_CHECK_FUNCS(getaddrinfo getnameinfo)
AC_CHECK_FUNCS(inet_ntoa getservbyport)
AC_CHECK_FUNCS(inet_ntop inet_pton)

AC_ARG_WITH(ipv6,
    AC_HELP_STRING([--with-ipv6],
        [enable IPv6 support]),
    [if test -z "$withval" ; then
        with_ipv6="yes"
    fi]
)

if test "x$with_ipv6" != "xno" ; then
    AC_MSG_CHECKING([whether to support IPv6])
    if test "$ac_cv_func_getaddrinfo" = "no" ; then
        AC_MSG_RESULT([no])
    else
        AC_DEFINE(INET6, 1, [IPv6 network protocol support])
        AC_MSG_RESULT([yes])
        ith_feature_append IPv6 support
    fi
else
    AC_MSG_RESULT([no])
fi

dnl ------------------------------------------------------------------------
dnl OpenSSL checks (code based on borrows from various places)
dnl ------------------------------------------------------------------------
AC_ARG_WITH(openssl,
    AC_HELP_STRING([--with-openssl=PATH],
        [use the OpenSSL library (PATH is OpenSSL's install prefix)]),
    [if test -z "$withval" ; then
        with_openssl="yes"
    fi]
)

if test "x$with_openssl" != "xno" ; then
    if test "x$with_openssl" = "xyes" -o -z "$with_openssl" ; then
        AC_MSG_CHECKING([for an OpenSSL installation])
    else
        openssl_dirs="$with_openssl"
        AC_MSG_CHECKING([for an OpenSSL installation in $with_openssl])
    fi

    old_LIBS="$LIBS"
    old_CFLAGS="$CFLAGS"
    LIBS="$LIBS -lssl -lcrypto"
    AC_TRY_LINK([#include <openssl/ssl.h>],[SSL_accept(NULL);],
        have_openssl="yes")

    if test -z "$have_openssl" ; then
        for dir in "$openssl_dirs" /usr/local /usr/ssl /usr/openssl \
                /usr/local/ssl /usr/local/openssl ; do 
            if test -n "$have_openssl" -o -z "$dir"; then
                break;
            fi

            if test -f "$dir/include/openssl/ssl.h" -a -f \
                    "$ssldir/lib/libssl.a" ; then
                CFLAGS="$CFLAGS -I$dir/include"
                LIBS="$old_LIBS -L$dir/lib -lssl -lcrypto"
                AC_TRY_LINK([#include <openssl/ssl.h>],[SSL_accept(NULL);],
                    have_openssl="yes")
            fi
        done
    fi

    if test -n "$have_openssl" ; then
        AC_DEFINE(HAVE_OPENSSL, 1, [OpenSSL Library Support])
        AC_MSG_RESULT(found)
        ith_feature_append SSL support
    else
        CLFAGS="$old_CFLAGS"
        LIBS="$old_LIBS"
        AC_MSG_RESULT(not found)
    fi
fi

dnl ------------------------------------------------------------------------
dnl Local configuration stuff below
dnl ------------------------------------------------------------------------
MODULES="*"
AC_ARG_WITH(modules,
    AC_HELP_STRING([--with-modules="list"], 
        [list of modules to build and install (defaults to * in modules/)]),
    if test -n "$withval " ; then
        if test "$withval" != "yes" -a "$withval" != "no" ; then
            MODULES="$withval"
        fi
    fi
)
AC_SUBST(MODULES)
AC_ARG_WITH(dmalloc,
    AC_HELP_STRING([--with-dmalloc=PATH],
        [use the debug malloc library (PATH is dmalloc's install prefix)]),
    if test "x$withval" != "no" ; then
        if test -z "$withval" -o "x$withval" = "xyes" ; then
            dmallocdir="/usr/local"
        else
            dmallocdir="$withval"
        fi
        LDFLAGS="$LDFLAGS -L$dmallocdir/lib"
        CFLAGS="$CFLAGS -I$dmallocdir/include"
        AC_CHECK_LIB(dmalloc, dmalloc_verify,,
            AC_MSG_ERROR([I cannot find the dmalloc library on your system])) 
        AC_DEFINE(USE_DMALLOC, 1, [dmalloc library support])
    fi
)
AC_ARG_ENABLE(debug,
    AC_HELP_STRING([--enable-debug],
        [enable debugging in code (changes CFLAGS, adds code)]),
    if test -n "$GCC" ; then
        CFLAGS="$CFLAGS -O0 -g3 -Wall -Wshadow -Wmissing-declarations -Wpointer-arith -Wcast-align -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Winline -Wbad-function-cast"
    fi
    AC_DEFINE(DEBUG_CODE, 1, [Extra debugging instructions])
)
AC_ARG_ENABLE(custom-libc,
    AC_HELP_STRING([--enable-custom-libc],
        [enable custom libc functions (may not work on all systems)]),
    ith_feature_append internal libc functions,
    AC_DEFINE(NO_INTERNAL_LIBC, 1, [No internal libc functions]),
)
AC_ARG_ENABLE(fd-setsize,
    AC_HELP_STRING([--enable-fd-setsize],
        [set FD_SETSIZE to the specified value (useful only if your system is
using select() and you you want more file descriptors to work)]),
    AC_DEFINE_UNQUOTED(FD_SETSIZE, $enable_fd_setsize,
        [the size of FD_SETSIZE]),
)

DIRSUFFIX="/$PACKAGE"
AC_ARG_ENABLE(dir-suffix,
    AC_HELP_STRING([--disable-dir-suffix],
        [keep 'make install' from appending the package name to target
installation directories]),
    DIRSUFFIX=""
)
AC_SUBST(DIRSUFFIX)

AC_OUTPUT(Makefile source/Makefile modules/Makefile)

dnl ------------------------------------------------------------------------
dnl finally, output our configuration to the user.
dnl ------------------------------------------------------------------------

eval "realbin=${bindir}"
eval "realbin=${realbin}"
eval "reallib=${libdir}${DIRSUFFIX}"
eval "reallib=${reallib}"
eval "realconf=${sysconfdir}${DIRSUFFIX}"
eval "realconf=${realconf}"
eval "realdata=${datadir}${DIRSUFFIX}"
eval "realdata=${realdata}"

AC_MSG_RESULT([
ithildin configuration
-----------------------])
    echo "ithildin version       : ${VERSION}"
    echo "host system            : ${host_cpu}-${host_vendor}-${host_os}"
    echo "compiler               : ${CC}"
    echo "compiler flags         : ${CFLAGS}"
    echo "module compiler flags  : ${CFLAGS} ${CFLAGSDLL}"
    echo "linker flags           : ${LDFLAGS} ${EXTRA_LDFLAGS}"
    echo "module linker          : ${CCDLL}"
    echo "module linker flags    : ${LDFLAGS} ${LDFLAGSDLL}"
    echo "extra features         : ${extra_features}"
    echo "socket polling method  : ${POLLER}"
    echo -n "building modules       : "
if test "$MODULES" = "*" ; then
    echo "(all modules available)"
else
    echo "$MODULES"
fi
    echo "binary install path    : ${realbin}"
    echo "library install path   : ${reallib}"
    echo "configuration data path: ${realconf}"
    echo "static data path       : ${realdata}"

