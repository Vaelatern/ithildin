/*
 * util.c: utility functions which don't fit elsewhere
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * a file full of functional tools that don't fall into any real category
 * heavily from other libraries which provide replacements.  There are also
 * generic functions in here which are not otherwise categorizeable.
 */

#include <ithildin/stand.h>

IDSTRING(rcsid, "$Id: util.c 578 2005-08-21 06:37:53Z wd $");

/* remove the printf macros, since we call some functions here and we won't be
 * calling anything recursively! */
#ifndef NO_INTERNAL_PRINTF
#undef sprintf
#undef snprintf
#undef vsprintf
#undef vsnprintf
#endif

/* hm, I got an FPE with terabyte_2 and up, I don't think i can bitshift
 * more than 30 places?  redefined the big ones as their actual numerical
 * constants, as unreadable as that is */
#define exabyte_2 1152921504606846976ULL
#define exabyte_si 1000000000000000000ULL
#define petabyte_2 1125899906842624ULL
#define petabyte_si 1000000000000000ULL
#define terabyte_2 1099511627776ULL
#define terabyte_si 1000000000000ULL
#define gigabyte_2 1<<30
#define gigabyte_si 1000000000ULL
#define megabyte_2 1<<20
#define megabyte_si 1000000ULL
#define kilobyte_2 1<<10
#define kilobyte_si 1000ULL

char *canonize_make_human(int size, char type);

/* mmap a file into a single string */
char *mmap_file(char *file) {
    struct stat sb;
    char *data = NULL;
    FILE *fp = NULL;

    if (file == NULL)
        return NULL;

    if (stat(file, &sb))
        return NULL; /* couldn't stat it, bleah */

    /*
     * allocate sufficient room for the file's contents in data,
     * then suck the file into that space.
     */

    data = (char *)malloc((size_t)sb.st_size + 1);
    if (data == NULL)
        return NULL;

    fp = fopen(file, "r");
    if (fp==NULL) {
        free(data);
        return NULL;
    }
        
    fread(data, (size_t)sb.st_size, 1, fp);
    data[sb.st_size] = '\0'; /* null terminate it */
    fclose(fp);

    return data;
}

char **mmap_file_to_array(char *file) {
    char *mem = mmap_file(file);
    int count = 1;
    char **array = malloc(sizeof(char *) * (count + 1));
    char *s = mem;

    if (mem == NULL) {
        free(array);
        return NULL;
    }

    array[0] = mem;
    /* count newline bits.  */
    while (*s != '\0') {
        switch (*s) {
            case '\r':
                *s = '\0';
                break;
            case '\n':
                *s = '\0';
                if (*(s + 1) != '\0') {
                    array[count] = s + 1;
                    array = realloc(array, sizeof(char *) * (++count + 1));
                }
                break;
        }
        s++;
    }
    array[count] = NULL;

    return array;
}

/* this function is intended to safely grab the next line out of a file.  It
 * assumes the file doesn't have incredibly huge lines, and returns its result
 * in the given buffer (which will be free of \r\n).  If fgets returns NULL
 * (indicating failure) it returns NULL as well. */
char *sfgets(char *str, int size, FILE *fp) {
    int len;

    if (fgets(str, size, fp) != NULL) {
        str[size - 1] = '\0'; /* for safety */
        len = strlen(str) - 1; /* we treat len as an index into the array,
                                  so .. */
        while (str[len] == '\r' || str[len] == '\n')
            str[len--] = '\0';

        return str;
    } else
        return NULL;
}

struct timeval *subtract_timeval(struct timeval tv1, struct timeval tv2) {
    static struct timeval result;

    memset(&result, 0, sizeof(struct timeval));
    result.tv_sec = tv2.tv_sec - tv1.tv_sec;
    result.tv_usec = tv2.tv_usec - tv1.tv_usec;
    if (result.tv_usec < 0) {
        result.tv_sec--;
        result.tv_usec += 1000000;
    }
    return &result;
}

struct timeval *add_timeval(struct timeval tv1, struct timeval tv2) {
    static struct timeval result;

    memset(&result, 0, sizeof(struct timeval));
    result.tv_sec = tv2.tv_sec + tv1.tv_sec;
    result.tv_usec = tv2.tv_usec + tv1.tv_usec;
    if (result.tv_usec >= 1000000) {
        result.tv_sec++;
        result.tv_usec -= 1000000;
    }
    return &result;
}

char *canonize_size(uint64_t size) {
    char type = 'b';
    int smallsize;

    if (size > exabyte_si) {
        size /= petabyte_2;
        type = 'e';
    }
    else if (size > petabyte_si) {
        size /= terabyte_2;
        type = 'p';
    }
    else if (size > terabyte_si) {
        size /= gigabyte_2;
        type = 't';
    }

    else if (size > gigabyte_si) {
        size /= megabyte_2;
        type = 'g';
    }
    else if (size > megabyte_si) {
        size /= kilobyte_2;
        type = 'm';
    }
    else if (size > kilobyte_si)
        type = 'k';

    smallsize = size;
    return canonize_make_human(smallsize, type);
}

char *canonize_make_human(int size, char type) {
    static char sizebuf[12];
    double val = size;

    if (type == 'b') {
        sprintf(sizebuf, "%db", size);
        return sizebuf;
    }
        
    val /= kilobyte_2;
    if (val - ((double)((long)val)))
        sprintf(sizebuf, "%.3f%cb", val, type);
    else
        sprintf(sizebuf, "%.0f%cb", val, type);

    return sizebuf;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
