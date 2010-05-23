/*
 * hash.h: hash table structures and prototypes
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: hash.h 726 2006-05-07 07:24:49Z wd $
 */

#ifndef HASH_H
#define HASH_H

typedef struct hashtable hashtable_t;

SLIST_HEAD(hashbucket, hashent);
struct hashent {
    void    *ent;
    unsigned int hv;        /* full hash value (i.e. no modulus).  makes for
                               quick compares. */
    SLIST_ENTRY(hashent) lp;
};

struct hashtable {
    uint32_t size;          /* thte size of the hash table */
#define hashtable_size(x) ((x)->size)
    uint32_t entries;       /* number of entries in the hash table */
#define hashtable_count(x) ((x)->entries)
    struct hashbucket *table;   /* our table */
    size_t  keyoffset;      /* this stores the offset of the key from the
                               given structure */
    size_t  keylen;         /* the length of the key.  this CANNOT be 0.  If
                               you want to use variable length strings use the
                               flag below. */

    /* these are useful for debugging the state of the hash system */
#ifdef DEBUG_CODE
    int     max_per_bucket; /* most entries in a single bucket */
    uint32_t empty_buckets; /* number of empty buckets */
#endif
#define HASH_FL_NOCASE 0x1      /* ignore case (tolower before hash) */
#define HASH_FL_STRING 0x2      /* key is a nul-terminated string, treat len
                                   as a maximum length to hash */
#define HASH_FL_INSERTHEAD 0x4  /* insert values at the head of their
                                   respective buckets (default) */
#define HASH_FL_INSERTTAIL 0x8  /* insert values at the tail of their bucket */
    int            flags;
    /* the symbol for our comparison function, used in hash_find_ent().  This
     * behaves much like the compare function used in qsort().  This means that
     * a return of 0 (ZERO) means success!  (this lets you use stuff like
     * strncmp easily).  We expect a symbol with a type of:
     * int (*)(void *, void *, size_t).  If the value is NULL then the memcmp
     * function is used instead (with a little kludge-work ;) */
    struct msymbol *cmpsym;
};

/* table management functions */
hashtable_t *create_hash_table(uint32_t, size_t, size_t, int, const char *);
void destroy_hash_table(hashtable_t *);
int hash_insert(hashtable_t *, void *);
int hash_delete(hashtable_t *, void *);
void *hash_find(hashtable_t *, void *);
#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
