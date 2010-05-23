/*
 * ircstring.c: IRC string handling functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This provides a method for implementing simple 8-bit character maps for use
 * with special string check and comparison functions.  This functionality may
 * be moved to the core later, or changed to unicode support, or otherwise
 * extended in a more reasonable manner.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: ircstring.c 579 2005-08-21 06:38:18Z wd $");

/* translates strings of the form: "a-c\xxx..." to individual characters.
 * If there is a problem with the string, return 0. */
static int istr_translate(unsigned char[256], const unsigned char *, int);

static int istr_translate(unsigned char to[256], const unsigned char *from,
        int len) {
    unsigned char *mystr = malloc(len + 1);
    unsigned char *s = mystr;
    int i = 0;

    strncpy(mystr, from, len);
    mystr[len] = '\0';
    /* this is a two-pass deal.  first, translate any \xxx items into their
     * proper characters.  then do any character-character translations
     * necessary. */
    while (*s) {
        if (*s == '\\' &&
                (*(s + 1) <= '2' && *(s + 1) >= '0') &&
                (*(s + 2) <= '9' && *(s + 2) >= '0') &&
                (*(s + 3) <= '9' && *(s + 3) >= '0')) {
            mystr[i++] = ((*(s + 1) - 48) * 100) + ((*(s + 2) - 48) * 10) +
                (*(s + 3) - 48);
            s += 4;
        } else
            mystr[i++] = *s++;
    }
    mystr[i] = '\0';

    /* okay, \xxx translated, now take mystr data and push it into the 'to'
     * field. */
    i = 0;
    s = mystr;
    while (*s) {
        if (*(s + 1) == '-' && *(s + 2) != '\0') {
            unsigned char c;
            if (*s > *(s + 2)) {
                free(mystr);
                return 0;
            }
            for (c = *s;c < *(s + 2);c++)
                to[i++] = c;
            s += 2;
        }
        to[i++] = *s++;
    }
    to[i] = '\0';

    free(mystr);
    return 1;
}

int istr_map(const unsigned char *string, unsigned char map[256]) {
    unsigned char left[256], right[256]; /* left is our map of okay chars.
                                            right is our map of equivalent
                                            chars */
    unsigned char *s;

    s = (unsigned char *)strchr(string, ' ');
    if (!istr_translate(left, string, s - string))
        return 0;
    if (s != NULL)
        if (!istr_translate(right, s + 1, strlen(s + 1)))
            return 0;

    if (strlen(right) > strlen(left))
        return 0;

    memset(map, '\0', 256);
    /* now generate our map.  for each char in 'left', go through and set the
     * char in the map to that char.  it actually makes more sense below. */
    s = left;
    while (*s) {
        map[*s] = *s;
        s++;
    }
    s = right;
    while (*s) { /* this is dirty, but it's fast too */
        map[*s] = left[s - right];
        s++;
    }

    return 1;
}

int istr_okay(unsigned char map[256], const unsigned char *string) {
    while (*string)
        if (map[*string++] == '\0')
            return 0;

    return 1;
}

int istrcmp(unsigned char map[256], const unsigned char *one,
        const unsigned char *two) {
    while (map[*one] == map[*two++])
        if (*one++ == '\0')
            return 0;

    return (*one - *(two - 1));
}

int istrncmp(unsigned char map[256], const unsigned char *one,
        const unsigned char *two, int len) {
    if (len == 0)
        return 0;

    do {
        if (map[*one] != map[*two++])
            return (*one - *(two - 1));
        if (*one++ == '\0')
            break;
    } while (--len != 0);

    return 0;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
