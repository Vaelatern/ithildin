/*
 * stand.h: standard include file used project-wide
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: stand.h 620 2005-11-23 06:22:36Z wd $
 */

#ifndef STAND_H
#define STAND_H

/* first include configure 'config.h' */
#include "config.h"

/* fix for idiot Linux breakage where we have to define '_GNU_SOURCE' to get
 * RTLD_NEXT/RTLD_DEFAULT (and god knows what else) even though they are
 * explicitly specified in the SysV dl*() specs and LOTS OF NON GNU SYSTEMS USE
 * THEM. */
#ifdef HAVE_FEATURES_H
#define _GNU_SOURCE
#endif

/* global config files are included here */
#include <ctype.h>
#ifdef HAVE_ERRNO_H
# include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_LIMITS_H
# include <limits.h>
#endif
#ifdef HAVE_MATH_H
# include <math.h>
#endif
#ifdef HAVE_MEMORY_H
# include <memory.h>
#endif
#ifdef POLLER_POLL
# include <poll.h>
#endif
#include <signal.h>
#include <stdarg.h>

#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#else
# undef bool
# undef true
# undef false
# define false 0
# define true 1
# define bool _Bool
# if __STDC_VERSION__ < 199901L
typedef int _Bool;
# endif
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_SYS_MMAN_H
# include <sys/mman.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef POLLER_KQUEUE
# include <sys/event.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef HAVE_MACHINE_ENDIAN_H
# include <machine/endian.h>
#elif defined(WORDS_BIGENDIAN)
# if !defined(BIG_ENDIAN)
#  define BIG_ENDIAN 4321
# endif
# undef BYTE_ORDER
# define BYTE_ORDER BIG_ENDIAN
#endif

#ifdef HAVE_NETDB_H
# include <netdb.h>
#endif
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

/* many files depend on queue.h, put it at the top */
#include <ithildin/queue.h>

/* Handy stuff for handling printf-like functions and warnings in gcc (thanks
 * hop & larne @ EFnet) */
/* some useful gcc-only defines to tag purposefully unused variables, and to
 * tag functions as being printf-like */
#if defined(__GNUC__)
# define __UNUSED __attribute__ ((__unused__))
# define __PRINTF(x) __attribute__ ((format (printf, x, x + 1)))
# ifdef __FreeBSD__
#  define __PRINTF0(x) __attribute__ ((format (printf0, x, x + 1)))
# else
#  define __PRINTF0(x) __attribute__ ((format (printf, x, x + 1)))
# endif
#else
# define __UNUSED
# define __PRINTF(x)
# define __PRINTF0(x)
#endif
#define IDSTRING(var,string) static const char var[] __UNUSED = string

/* if we're debugging, don't inline stuff! */
#ifdef DEBUG_CODE
# undef inline
# define inline
#endif

#include <ithildin/conf.h>
#include <ithildin/event.h>
#include <ithildin/global.h>
#include <ithildin/hash.h>
#include <ithildin/log.h>
#include <ithildin/malloc.h>
#include <ithildin/md5.h>
#include <ithildin/module.h>
#include <ithildin/socket.h>
#include <ithildin/string.h>
#include <ithildin/timer.h>
#include <ithildin/util.h>

/* keep assert at the bottom in case any other headers get 'cute' */
#ifndef DEBUG_CODE
# define NDEBUG
#endif
#ifdef HAVE_ASSERT_H
# include <assert.h>
#endif

/* keep dmalloc at the bottom of the file to override any macros */
#ifdef USE_DMALLOC
# include <dmalloc.h>
# ifdef DEBUG_CODE
#  define DMALLOC_FUNC_CHECK
# endif
#endif

/* version numbers and stuff are defined here */
#define BASENAME_VER "ithildin"
#define MAJOR_VER 1
#define MINOR_VER 1
#define PATCH_VER 2

#define MODULE_HEADER_DATA {MAJOR_VER, MINOR_VER, PATCH_VER, 0}

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
