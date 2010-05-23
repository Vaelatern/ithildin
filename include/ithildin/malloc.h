/*
 * malloc.h: malloc support prototypes
 * 
 * Copyright 2003 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: malloc.h 578 2005-08-21 06:37:53Z wd $
 */

#ifndef MALLOC_H
#define MALLOC_H

#if !defined(NO_INTERNAL_LIBC) && !defined(USE_DMALLOC) &&                \
    defined(MALLOC_LOWLEVEL)
# define USE_INTERNAL_MALLOC
# ifdef MALLOC_OVERRIDE
#  define calloc ith_calloc
#  define free ith_free
#  define malloc ith_malloc
#  define realloc ith_realloc
# endif
/* the actual functions.. */
void *ith_calloc(size_t, size_t);
void ith_free(void *);
void *ith_malloc(size_t);
void *ith_realloc(void *, size_t);
#endif

#endif

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
