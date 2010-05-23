/*
 * hash.c: hash table support functions
 * 
 * Copyright 2002-2006 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This system implements a generic hash-table systeem.  The workings of
 * hash-tables are discussed in almost any simple algorithm book, so I won't
 * go into deteail here.
 */

#include <ithildin/stand.h>

IDSTRING(rcsid, "$Id: hash.c 726 2006-05-07 07:24:49Z wd $");

static void resize_hash_table(hashtable_t *table, uint32_t elems);
static unsigned int hash_get_key_hash(hashtable_t *, void *, size_t);

/*
 * This function creates a hashtable with 'elems' buckets (well, not really,
 * it returns the closest thing that is smaller and a power-of-two).
 * 'offset' is the offset of the key in structures which get passed to
 * hash_insert().  len is the length of the key (0 for \0 terminated
 * strings of variable length).  cmpfunc is the function which should be
 * used for comparsion when calling hash_find()
 *
 * Note that hash values are 32 bit.  As such we will not grow a table past
 * 2^31 entries.  I do not anticipate anyone using this arguably not very
 * good code for something of that magnitude at any rate.
 *
 * We do not create tables smaller than 128 elements.  If your table is that
 * small it is probable that hash tables are not actually going to help you
 * out much in terms of speed.
 */
hashtable_t *create_hash_table(uint32_t elems, size_t offset, size_t len,
	int flags, const char *cmpname) {
    hashtable_t *htp = malloc(sizeof(hashtable_t));
    uint32_t real_elems = 0x80;
    
    /* Take elems and begin shifting real_elems to the left until it is
     * larger than, or equal in size to, elems.  If it is equal just stop,
     * if it is larger shift it back down to the right one place and go with
     * that. */

    if (elems > 1<<31) {
        log_error("Request for hashtable of more than 2^31 elements cannot "
                "be fulfilled.");
        return NULL;
    }

    while (real_elems < elems)
        real_elems <<= 1;
    if (real_elems > elems)
        real_elems >>= 1;

    if (real_elems != elems)
        log_debug("Request for %d element hash table adjusted to %d "
                "elements", elems, real_elems);

    htp->size = real_elems;
    htp->entries = 0;
    htp->keyoffset = offset;
    htp->keylen = len;
#ifdef DEBUG_CODE
    htp->max_per_bucket = 1;
    htp->empty_buckets = real_elems;
#endif
    htp->flags = flags;
    if (cmpname != NULL)
	    htp->cmpsym = import_symbol((char *) cmpname);
    else
        htp->cmpsym = import_symbol("memcmp");

    htp->table = malloc(sizeof(struct hashbucket) * htp->size);
    memset(htp->table, 0, sizeof(struct hashbucket) * htp->size);

    return htp;
}

/* hash_table destroyer.  sweep through the given table and kill off every
 * hashent */
void destroy_hash_table(hashtable_t *table) {
    struct hashent *hep;
    int i;

    for (i = 0;i < table->size;i++) {
        while (!SLIST_EMPTY(&table->table[i])) {
            hep = SLIST_FIRST(&table->table[i]);
            SLIST_REMOVE_HEAD(&table->table[i], lp);
            free(hep);
        }
    }
    free(table->table);
    free(table);
}

/* this function is used to resize a hash table.  of course, to do this, it has
 * to create a new table and re-hash everything in it, so it's not very
 * efficient, however if used judiciously it should enhance performance, not
 * hinder it.  'elems' is the new size of the table. */
static void resize_hash_table(hashtable_t *table, uint32_t elems) {
    struct hashbucket *oldtable;
    int oldsize, i;
    struct hashent *hep;

    /* preserve the old table, then create a new one.  */
    oldtable = table->table;
    oldsize = table->size;
    table->size = elems;
    table->entries = 0;
#ifdef DEBUG_CODE
    table->max_per_bucket = 1;
    table->empty_buckets = elems;
#endif
    table->table = malloc(sizeof(struct hashbucket) * table->size);
    memset(table->table, 0, sizeof(struct hashbucket) * table->size);

    /* now walk each bucket in the old table, pulling off individual entries
     * and re-adding them to the table as we go */
    for (i = 0;i < oldsize;i++) {
        while (!SLIST_EMPTY(&oldtable[i])) {
            hep = SLIST_FIRST(&oldtable[i]);
            hash_insert(table, hep->ent);
            SLIST_REMOVE_HEAD(&oldtable[i], lp);
            free(hep);
        }
    }
    free(oldtable);
}

/*
 * NOTE
 * This hash algorithm was not written by me.  I am math-stupid, so I spent
 * some time investigating various algorithms and liked the cut of this
 * one's jib.  I tested it on some datasets I had lying around and find it
 * very adequate, as well as performant.  The original author's information:
 *  
 * By Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.  You may use this
 * code any way you wish, private, educational, or commercial.  It's free.
 * See http://burtleburtle.net/bob/hash/evahash.html
 *
 * I have reworked some of this code for updated formatting and to make it
 * more readable (to me) but I have not tweaked the algorithm at all.  I
 * also took out a lot of the comments about how the algorithm works, the
 * website above should be used as a reference.
 */
#define mix(a,b,c) {                                                          \
  a -= b; a -= c; a ^= (c>>13);                                               \
  b -= c; b -= a; b ^= (a<<8);                                                \
  c -= a; c -= b; c ^= (b>>13);                                               \
  a -= b; a -= c; a ^= (c>>12);                                               \
  b -= c; b -= a; b ^= (a<<16);                                               \
  c -= a; c -= b; c ^= (b>>5);                                                \
  a -= b; a -= c; a ^= (c>>3);                                                \
  b -= c; b -= a; b ^= (a<<10);                                               \
  c -= a; c -= b; c ^= (b>>15);                                               \
}

/* this function allows you to get the hash of a given key.  it must be used in
 * the context of the table, of course.  it is mostly useful for insert/delete
 * below, and also for searching, but the included function should do that for
 * you adequately. */
uint32_t hash_get_key_hash(hashtable_t *table, void *key,
        size_t offset) {
    register uint32_t a, b, c;
    register uint32_t len;
    uint32_t length;
    unsigned char *ckey = (unsigned char *)key + offset;

    /* Set up the internal state */
    if (table->flags & HASH_FL_STRING) {
        length = strlen(ckey);
        if (length > table->keylen)
            /* they may ask to only hash on the first n bytes ... 
             * XXX: What if they actually want to hash the LAST n bytes (say
             * for a filename or URL)?  There ought to be a flag... */
            length = table->keylen;
    } else
        length = table->keylen;
   
    len = length;
    a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
    c = 0xb33ff00d;      /* the previous hash value (UNUSED initval
                            functionality) */

    /* XXX: So in order to support case insensitive hashing I simply took
     * the below code and duped it further down to key generation on those
     * strings.  I think this will end up with faster conpiled code, though
     * it is not pleasant to look at..  */
    if (table->flags & HASH_FL_NOCASE) {
        while (len >= 12) {
            a += ( (uint32_t)tolower(ckey[0]) +
                  ((uint32_t)tolower(ckey[1]) << 8) +
                  ((uint32_t)tolower(ckey[2]) << 16) +
                  ((uint32_t)tolower(ckey[3]) << 24));
            b += ( (uint32_t)tolower(ckey[5]) +
                  ((uint32_t)tolower(ckey[6]) << 8) +
                  ((uint32_t)tolower(ckey[7]) << 16) +
                  ((uint32_t)tolower(ckey[8]) << 24));
            c += ( (uint32_t)tolower(ckey[9]) +
                  ((uint32_t)tolower(ckey[10]) << 8) +
                  ((uint32_t)tolower(ckey[11]) << 16) +
                  ((uint32_t)tolower(ckey[12]) << 24));
           mix(a, b, c);
           ckey += 12; len -= 12;
        }

        c += length;

        /* deal with the last 11 (or less) bytes */
        switch (len) { /* all the case statements fall through */
        case 11: c += ((uint32_t)tolower(ckey[10]) << 24);
        case 10: c += ((uint32_t)tolower(ckey[9]) << 16);
        case 9:  c += ((uint32_t)tolower(ckey[8]) << 8);
           /* the first byte of c is reserved for the length */
        case 8:  b += ((uint32_t)tolower(ckey[7]) << 24);
        case 7:  b += ((uint32_t)tolower(ckey[6]) << 16);
        case 6:  b += ((uint32_t)tolower(ckey[5]) << 8);
        case 5:  b += ((uint32_t)tolower(ckey[4]));
        case 4:  b += ((uint32_t)tolower(ckey[3]) << 24);
        case 3:  b += ((uint32_t)tolower(ckey[2]) << 16);
        case 2:  b += ((uint32_t)tolower(ckey[1]) << 8);
        case 1:  b += ((uint32_t)tolower(ckey[0]));
        /* case 0: nothing left to add */
        }
        mix(a, b, c);
    } else {
        while (len >= 12) {
            a += ( (uint32_t)ckey[0] +
                  ((uint32_t)ckey[1] << 8) +
                  ((uint32_t)ckey[2] << 16) +
                  ((uint32_t)ckey[3] << 24));
            b += ( (uint32_t)ckey[5] +
                  ((uint32_t)ckey[6] << 8) +
                  ((uint32_t)ckey[7] << 16) +
                  ((uint32_t)ckey[8] << 24));
            c += ( (uint32_t)ckey[9] +
                  ((uint32_t)ckey[10] << 8) +
                  ((uint32_t)ckey[11] << 16) +
                  ((uint32_t)ckey[12] << 24));
           mix(a, b, c);
           ckey += 12; len -= 12;
        }

        c += length;

        /* deal with the last 11 (or less) bytes */
        switch (len) { /* all the case statements fall through */
        case 11: c += ((uint32_t)ckey[10] << 24);
        case 10: c += ((uint32_t)ckey[9] << 16);
        case 9:  c += ((uint32_t)ckey[8] << 8);
           /* the first byte of c is reserved for the length */
        case 8:  b += ((uint32_t)ckey[7] << 24);
        case 7:  b += ((uint32_t)ckey[6] << 16);
        case 6:  b += ((uint32_t)ckey[5] << 8);
        case 5:  b += ((uint32_t)ckey[4]);
        case 4:  b += ((uint32_t)ckey[3] << 24);
        case 3:  b += ((uint32_t)ckey[2] << 16);
        case 2:  b += ((uint32_t)ckey[1] << 8);
        case 1:  b += ((uint32_t)ckey[0]);
        /* case 0: nothing left to add */
        }
        mix(a, b, c);
    }

    /* just return the hash sized right for the table instead of asking them
     * to do it.. */
    return c & (table->size - 1);
}

/* add the entry into the table */
int hash_insert(hashtable_t *table, void *ent) {
    unsigned int hash = hash_get_key_hash(table, ent, table->keyoffset);
    struct hashent *hep = malloc(sizeof(struct hashent));

    hep->hv = hash;
#ifdef DEBUG_CODE
    if (SLIST_EMPTY(&table->table[hash]))
        table->empty_buckets--; /* this bucket isn't empty now */
    else {
        /* count the items in the bucket.  of course this is wasteful, that's
         * why you don't debug code in production. :) */
        struct hashent *hep2;
        int cnt = 0;
        SLIST_FOREACH(hep2, &table->table[hash], lp) {
            cnt++;
        }
        if (cnt > table->max_per_bucket)
            table->max_per_bucket = cnt;
    }
#endif

    table->entries++;
    hep->ent = ent;
    if (table->flags & HASH_FL_INSERTTAIL) {
        /* find the end of the list and insert data there.  this is costly,
         * and probably not something you want to do unless you're really sure
         * that it's what you're after... i.e. if you think new entries will
         * be the least sought for, and you expect a lot of collisions, you
         * would use this.. otherwise I don't know why.. */
        struct hashent *hep2;

        hep2 = SLIST_FIRST(&table->table[hash]);
        while (hep2 != NULL)
            if (SLIST_NEXT(hep2, lp) == NULL)
                break;
            else
                hep2 = SLIST_NEXT(hep2, lp);

        if (hep2 == NULL)
            SLIST_INSERT_HEAD(&table->table[hash], hep, lp);
        else
            SLIST_INSERT_AFTER(hep2, hep, lp);
    } else
        SLIST_INSERT_HEAD(&table->table[hash], hep, lp);

    /*
     * if the table has 1.2x as many entries as there are buckets, resize it so
     * that there ar twice as many buckets. :)
     * Perf considerations:
     * I have heard a lot of conjecture about the wisdom of resizes in
     * relation to hash tables, some advise resizing at about 70-80%
     * capacity, others claim that a good hash algorithm (of which FNC is
     * ostensibly one) should be O(1) even when the table has as many
     * entries as it does buckets.  Some basic testing from my standpoint
     * shows that FNV does a good enough job of distribution that we really
     * don't need to worry about taking the hit until we have more entries
     * than buckets.  Furthermore, in what would be mediocre circumstances
     * (50% buckets unused, most buckets doubled or tripled) the runtime is
     * still not very high, whereas the costs associated with a resize
     * (in terms of memory and resizing operation considerations) are
     * relatively high.  especially when data growth is rapid, the less we
     * resize the better off we are.
     *
     * TODO: Allow consumers of hash tables to specify a growth ratio
     * instead of making this static.
     */
    if (((table->size * 6) / 5) <= table->entries)
        resize_hash_table(table, table->size * 2);

    return 1;
}

/* remove the entry from the table. */
int hash_delete(hashtable_t *table, void *ent) {
    unsigned int hash = hash_get_key_hash(table, ent, table->keyoffset);
    struct hashent *hep;

    SLIST_FOREACH(hep, &table->table[hash], lp) {
        if (hep->ent == ent)
            break;
    }
    if (hep == NULL)
        return 0;

    table->entries--;
    SLIST_REMOVE(&table->table[hash], hep, hashent, lp);
    free(hep);

#ifdef DEBUG_CODE
    if (SLIST_EMPTY(&table->table[hash]))
        table->empty_buckets++; /* this bucket is empty again. */
#endif

    return 1;
}

/* last, but not least, the find functions.  given the table and the key to
 * look for, it hashes the key, and then calls the compare function in the
 * given table slice until it finds the item, or reaches the end of the
 * list. */
void *hash_find(hashtable_t *table, void *key) {
    unsigned int hash = hash_get_key_hash(table, key, 0);
    struct hashbucket *bucket = &table->table[hash];
    struct hashent *hep;
    int (*cmpfunc)(void *, void *, size_t) =
        (int (*)(void *, void *, size_t))getsym(table->cmpsym);

    SLIST_FOREACH(hep, bucket, lp) {
        if (hep->hv == hash && !cmpfunc(&((char *)hep->ent)[table->keyoffset],
                    key, table->keylen))
            return hep->ent;
    }

    return NULL; /* not found */
}

/* I haven't found a use for this code yet... */
#if 0
static struct hashent *hash_find_ent(hashtable_t *, void *);
static struct hashent *hash_find_ent_next(hashtable_t *, void *, struct hashent *);

/* this function finds the first 'hashent' of the given key, it returns a
 * hashent so that calls to the _next variant can be a bit faster. */
static struct hashent *hash_find_ent(hashtable_t *table, void *key) {
    unsigned int hash = hash_get_key_hash(table, key, 0);
    struct hashbucket *bucket = &table->table[hash];
    struct hashent *hep;
    int (*cmpfunc)(void *, void *, size_t) =
        (int (*)(void *, void *, size_t))getsym(table->cmpsym);

    SLIST_FOREACH(hep, bucket, lp) {
        if (hep->hv == hash && !cmpfunc(&((char *)hep->ent)[table->keyoffset],
                    key, table->keylen))
            return hep;
    }

    return NULL; /* not found */
}

/* this function finds the next entry matching the key after 'ent' */
static struct hashent *hash_find_ent_next(hashtable_t *table, void *key,
        struct hashent *ent) {
    struct hashent *hep = ent;
    int (*cmpfunc)(void *, void *, size_t) =
        (int (*)(void *, void *, size_t))getsym(table->cmpsym);

    /* we know where we are .. */
    while ((hep = SLIST_NEXT(hep, lp)) != NULL) {
        if (!cmpfunc(&((char *)hep->ent)[table->keyoffset], key,
                    table->keylen))
            return hep;
    }

    return NULL;
}
#endif
    
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
