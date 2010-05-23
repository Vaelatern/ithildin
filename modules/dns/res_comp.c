/*
 * Copyright (c) 1985 Regents of the University of California. All
 * rights reserved.
 * 
 * Redistribution and use in source and binary forms are permitted
 * provided that: (1) source distributions retain this entire copyright
 * notice and comment.
 * Neither the name of the University nor the names of its contributors may be
 * used to endorse or promote products derived from this software without
 * specific prior written permission.  THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE.
 */

#include <ithildin/stand.h>

#include "dns.h"

IDSTRING(rcsid, "$Id: res_comp.c 579 2005-08-21 06:38:18Z wd $");

static int dn_find(unsigned char *exp_dn, unsigned char *msg,
        unsigned char **dnptrs, unsigned char **lastdnptr);

/* Defines for handling compressed domain names */
#define INDIR_MASK   0xc0

/*
 * Expand compressed domain name 'comp_dn' to full domain name. 'msg'
 * is a pointer to the begining of the message, 'eomorig' points to the
 * first location after the message, 'exp_dn' is a pointer to a buffer
 * of size 'length' for the result. Return size of compressed name or
 * -1 if there was an error.
 */
int dn_expand(unsigned char *msg, unsigned char *eomorig,
        unsigned char *comp_dn, unsigned char *exp_dn, int length) {
    unsigned char *cp, *dn;
    int n, c;
    unsigned char *eom;
    int len = -1, checked = 0;

    dn = exp_dn;
    cp = comp_dn;
    eom = exp_dn + length;
    /* fetch next label in domain name */
    while ((n = *cp++)) {
        /* Check for indirection */
        switch (n & INDIR_MASK) {
            case 0:
                if (dn != exp_dn) {
                    if (dn >= eom)
                        return (-1);
                    *dn++ = '.';
                }
                if (dn + n >= eom)
                    return (-1);
                    checked += n + 1;
                while (--n >= 0) {
                    if ((c = *cp++) == '.') {
                        if (dn + n + 2 >= eom)
                                return (-1);
                        *dn++ = '\\';
                    }
                    *dn++ = c;
                        if (cp >= eomorig)        /* out of range */
                        return (-1);
                }
                break;

            case INDIR_MASK:
                if (len < 0)
                    len = cp - comp_dn + 1;
                cp = msg + (((n & 0x3f) << 8) | (*cp & 0xff));
                if (cp < msg || cp >= eomorig)        /* out of range */
                    return (-1);
                checked += 2;
                /* Check for loops in the compressed name; if we've
                   looked at the whole message, there must be a loop. */
                if (checked >= eomorig - msg)
                    return (-1);
                break;

            default:
                return (-1);        /* flag error */
        }
    }
    *dn = '\0';
    if (len < 0)
        len = cp - comp_dn;
    return (len);
}
/* Compress domain name 'exp_dn' into 'comp_dn'. Return the size of the
 * compressed name or -1. 'length' is the size of the array pointed to
 * by 'comp_dn'. 'dnptrs' is a list of pointers to previous compressed
 * names. dnptrs[0] is a pointer to the beginning of the message. The
 * list ends with NULL. 'lastdnptr' is a pointer to the end of the
 * arrary pointed to by 'dnptrs'. Side effect is to update the list of
 * pointers for labels inserted into the message as we compress the
 * name. If 'dnptr' is NULL, we don't try to compress names. If
 * 'lastdnptr' is NULL, we don't update the list.  */
int dn_comp(unsigned char *exp_dn, unsigned char *comp_dn, int length,
        unsigned char **dnptrs, unsigned char **lastdnptr) {
    unsigned char *cp, *dn;
    int c, l;
    unsigned char **cpp, **lpp, *sp, *eob;
    unsigned char *msg;

    dn = exp_dn;
    cp = comp_dn;
    eob = cp + length;
    cpp = lpp = NULL; /* fix for warning */
    if (dnptrs != NULL) {
        if ((msg = *dnptrs++) != NULL) {
            for (cpp = dnptrs; *cpp != NULL; cpp++);
                lpp = cpp;                /* end of list to search */
        }
    }
    else
        msg = NULL;

    for (c = *dn++; c != '\0';) {
        /* look to see if we can use pointers */
        if (msg != NULL) {
            if ((l = dn_find(dn - 1, msg, dnptrs, lpp)) >= 0) {
                if (cp + 1 >= eob)
                    return (-1);
                *cp++ = (l >> 8) | INDIR_MASK;
                *cp++ = l % 256;
                return (cp - comp_dn);
            }
            /* not found, save it */
            if (lastdnptr != NULL && cpp < lastdnptr - 1) {
                *cpp++ = cp;
                *cpp = NULL;
            }
        }
        sp = cp++;                /* save ptr to length byte */
        do {
            if (c == '.') {
                c = *dn++;
                break;
            }
            if (c == '\\') {
                if ((c = *dn++) == '\0')
                break;
            }
            if (cp >= eob) {
                if (msg != NULL)
                *lpp = NULL;
                return (-1);
            }
            *cp++ = c;
        } while ((c = *dn++) != '\0');

        /* catch trailing '.'s but not '..' */
        if ((l = cp - sp - 1) == 0 && c == '\0') {
            cp--;
            break;
        }
        if (l <= 0 || l > DNS_MAX_SEGLEN) {
            if (msg != NULL)
                *lpp = NULL;
            return (-1);
        }
        *sp = l;
    }
    if (cp >= eob) {
        if (msg != NULL)
            *lpp = NULL;
        return (-1);
    }
    *cp++ = '\0';
    return (cp - comp_dn);
}
/*
 * Skip over a compressed domain name. Return the size or -1.
 */
int dn_skipname(unsigned char *comp_dn, unsigned char *eom) {
    unsigned char *cp;
    int n;

    cp = comp_dn;
    while (cp < eom && (n = *cp++)) {
        /* check for indirection */
        switch (n & INDIR_MASK) {
            case 0:                /* normal case, n == len */
                cp += n;
                continue;
            default:                /* illegal type */
                return (-1);
            case INDIR_MASK:        /* indirection */
                cp++;
        }
        break;
    }
    return (cp - comp_dn);
}
/*
 * Search for expanded name from a list of previously compressed names.
 * Return the offset from msg if found or -1. dnptrs is the pointer to
 * the first name on the list, not the pointer to the start of the
 * message.
 */
static int dn_find(unsigned char *exp_dn, unsigned char *msg,
        unsigned char **dnptrs, unsigned char **lastdnptr) {
    unsigned char *dn, *cp, **cpp;
    int n;
    unsigned char *sp;

    for (cpp = dnptrs; cpp < lastdnptr; cpp++) {
        dn = exp_dn;
        sp = cp = *cpp;
        while ((n = *cp++)) {
            /* check for indirection */
            switch (n & INDIR_MASK) {
                case 0:                /* normal case, n == len */
                    while (--n >= 0) {
                        if (*dn == '.')
                            goto next;
                        if (*dn == '\\')
                            dn++;
                        if (*dn++ != *cp++)
                            goto next;
                    }
                    if ((n = *dn++) == '\0' && *cp == '\0')
                        return (sp - msg);
                    if (n == '.')
                        continue;
                    goto next;

                default:                /* illegal type */
                    return (-1);

                case INDIR_MASK:        /* indirection */
                    cp = msg + (((n & 0x3f) << 8) | *cp);
            }
        }
        if (*dn == '\0')
            return (sp - msg);
next:;
    }
    return (-1);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
