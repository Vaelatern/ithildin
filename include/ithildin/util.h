/*
 * util,h: prototypes for various utility functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: util.h 576 2005-08-21 06:33:26Z wd $
 */

#ifndef UTIL_H
#define UTIL_H

/* $Id: util.h 576 2005-08-21 06:33:26Z wd $ */

/* map a file into memory in various ways */
char *mmap_file(char *);
char **mmap_file_to_array(char *);

char *sfgets(char *, int, FILE *);

/* these are wrappers for malloc() family functions */
void *calloc_wrap(size_t number, size_t size, char *file, int line);
void free_wrap(void *memory, char *file, int line);
void *malloc_wrap(size_t size, char *file, int line);
void *realloc_wrap(void *ptr, size_t size, char *file, int line);

/* subtract timeval 1 from timeval 2 */
struct timeval *subtract_timeval(struct timeval tv1, struct timeval tv2);
/* add timeval 1 and timeval 2 */
struct timeval *add_timeval(struct timeval tv1, struct timeval tv2);

/* take a file size and turn it into a string measure (e.g. 1024 = 1kb) */
char *canonize_size(uint64_t size);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
