/*
 * ircstring.h: IRC string support header
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: ircstring.h 577 2005-08-21 06:33:51Z wd $
 */

#ifndef IRCD_STRING_H
#define IRCD_STRING_H

/* generate a character table for use with istr* functions.  the table is
 * passed along with a string representation of the characters in it.  the
 * string should be of the form:
 * "allowedchars equivalents"
 * ' ' cannot be an allowed character.  For instance, if you want to
 * generate a case-insensitive table for nicknames, where the characters
 * 'a-z0-9[{]}\|`^-_' are allowed, you would want:
 * "a-z0-9[]{}\|`^-_ A-Z" (to make it so that a-z == A-Z).  If you wanted to
 * represent the old RFC1459 charset, you would do, instead:
 * "a-z[]\0-9`^-_ A-Z{}|" (I hope this makes the mapping clear) 
 *
 * returns 1 if the string was interpreted correctly, 0 otherwise. */
int istr_map(const unsigned char *, unsigned char[256]);

/* check to see that a given string contains only valid characters */
int istr_okay(unsigned char[256], const unsigned char *);

/* these are just like strcmp/strncmp, except they use the given character
 * map */
int istrcmp(unsigned char[256], const unsigned char *, const unsigned char *);
int istrncmp(unsigned char[256], const unsigned char *, const unsigned char *,
        int);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
