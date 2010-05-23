/*
 * string.c: string manipulation functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * this file contains a variety of string manipulation functions.  some of them
 * are libc replacements which were designed to be as fast as possible for the
 * intended purposes of the software, there are also a load of others which
 * serve many varied purposes.
 */

#include <ithildin/stand.h>

IDSTRING(rcsid, "$Id: string.c 817 2008-06-23 19:25:19Z wd $");

unsigned char tolowertab[256] = {
0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7,  0x8,  0x9,  0xa,  0xb,
0xc,  0xd,  0xe,  0xf, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,  ' ',  '!',  '"',  '#',
'$',  '%',  '&', '\'',  '(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',
'0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  ':',  ';',
'<',  '=',  '>',  '?',  '@',  'a',  'b',  'c',  'd',  'e',  'f',  'g',
'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',  'p',  'q',  'r',  's',
't',  'u',  'v',  'w',  'x',  'y',  'z',  '[', '\\',  ']',  '^',  '_',
'`',  'a',  'b',  'c',  'd',  'e',  'f',  'g',  'h',  'i',  'j',  'k',
'l',  'm',  'n',  'o',  'p',  'q',  'r',  's',  't',  'u',  'v',  'w',
'x',  'y',  'z',  '{',  '|',  '}',  '~', 0x7f, 0x80, 0x81, 0x82, 0x83,
0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b,
0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3,
0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb,
0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3,
0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb,
0xfc, 0xfd, 0xfe, 0xff
};

unsigned char touppertab[256] = {
0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7,  0x8,  0x9,  0xa,  0xb,
0xc,  0xd,  0xe,  0xf, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,  ' ',  '!',  '"',  '#',
'$',  '%',  '&', '\'',  '(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',
'0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  ':',  ';',
'<',  '=',  '>',  '?',  '@',  'A',  'B',  'C',  'D',  'E',  'F',  'G',
'h',  'I',  'J',  'K',  'L',  'M',  'N',  'O',  'P',  'Q',  'R',  'S',
't',  'U',  'V',  'W',  'X',  'Y',  'Z',  '[', '\\',  ']',  '^',  '_',
'`',  'A',  'B',  'C',  'D',  'E',  'F',  'G',  'H',  'I',  'j',  'K',
'L',  'M',  'N',  'O',  'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',
'X',  'Y',  'Z',  '{',  '|',  '}',  '~', 0x7f, 0x80, 0x81, 0x82, 0x83,
0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b,
0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3,
0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb,
0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3,
0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb,
0xfc, 0xfd, 0xfe, 0xff
};

/* str* functions */

int ith_strcasecmp(const char *one, const char *two) {
    while (tolower(*one) == tolower(*two++))
        if (*one++ == '\0')
            return 0;
    return (tolower(*one) - tolower(*--two));
}
int ith_strncasecmp(const char *one, const char *two, int len) {
    if (len != 0) {
        do {
            if (tolower(*one) != tolower(*two++))
                return (tolower(*one) - tolower(*--two));
            if (*one++ == '\0')
                break;
        } while (--len != 0);
    }
    return 0;
}
int ith_strcmp(const char *one, const char *two) {
    while (*one == *two++)
        if (*one++ == '\0')
            return 0;
    return (*one - *--two);
}
int ith_strncmp(const char *one, const char *two, int len) {

    if (len != 0) {
        do {
            if (*one != *two++)
                return (*one - *--two);
            if (*one++ == '\0')
                break;
        } while (--len != 0);
    }
    return 0;
}

char *ith_strcat(char *str, const char *app) {
    char *s = str;

    while (*s != '\0')
        s++;
    while ((*s++ = *app++) != '\0');

    return str;
}

char *ith_strncat(char *str, const char *app, int len) {
    
    if (len != 0) {
        char *s = str;
        const char *a = app;
        
        while (*s != '\0')
            s++;

        do {
            if ((*s = *a++) == '\0')
                break;
            s++;
        } while (--len != 0);
        *s = '\0';
    }
    return str;
}

char *ith_strchr(const char *str, char ch) {
    
    while (1) {
        if (*str == ch)
            return (char *)str;
        if (*str++ == '\0')
            return NULL;
    }
}

char *ith_strrchr(const char *str, char ch) {
    char *last = NULL;
    while (1) {
        if (*str == ch)
            last = (char *)str;
        if (*str++ == '\0')
            return last;
    }
}

char *ith_strcpy(char *to, const char *from) {
    char *ret = to;
    while ((*to++ = *from++) != '\0');
    return ret;
}
char *ith_strncpy(char *to, const char *from, int len) {
    if (len != 0) {
        char *t = to;
        const char *f = from;

        do {
            if ((*t++ = *f++) == '\0') {
                while (--len != 0)
                    *t++ = '\0';
                break;
            }
        } while (--len != 0);
    }
    return to;
}

char *ith_strdup(const char *str) {
    size_t len;
    char *s;

    len = strlen(str) + 1;
    if ((s = malloc(len)) != NULL)
        memcpy(s, str, len);
    return s;
}

size_t ith_strlen(const char *str) {
    const char *s = str;

    while (*s)
        s++;

    return (s - str);
}

/* this function was (sort of) borrowed from FreeBSD 4's libc.  I've shuffled a
 * bit around and tried to make it more clear what's going on.  The general
 * idea is that you can call strsep() with the address of a string to work on,
 * and a list of delimiting characters.  it will break up the string as it is
 * called, returning one argument at a time.  it may return an empty argument
 * which can be detected by checking to see if the return value starts with a
 * \0 character. */
char *ith_strsep(char **from, const char *delim) {
    char *s, *dp, *ret;

    if ((s = *from) == NULL)
        return NULL;

    ret = s;
    while (*s != '\0') {
        /* loop until the end of s, checking against each delimiting character,
         * if we find a delimiter set **s to '\0' and return our previous token
         * to the user. */
        dp = (char *)delim;
        while (*dp != '\0') {
            if (*s == *dp) {
                *s = '\0';
                *from = s + 1;
                return ret;
            }
            dp++;
        }
        s++;
    }
    /* end of string case */
    *from = NULL;
    return ret;
}

/* ith_*printf stuff below */

char num[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
char otoa_tab[8] = { '0', '1', '2', '3', '4', '5', '6', '7' };
char itoa_tab[10] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'  };
char xtoa_tab[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 
          'a', 'b', 'c', 'd', 'e', 'f'  };
char nullstring[]="(null)";

#undef sprintf
#undef snprintf
#undef vsprintf
#undef vsnprintf

int ith_vsnprintf(char *str, size_t size, const char *pattern, va_list ap) {
    char *s;
    char *buf = str;
    const char *format = pattern;
    int q = 0, u = 0, x = 0;
    unsigned long i;
    uint64_t i64;
    size_t len = 0;
    char *tab;
    int modulo;

    /* if size is 0, just set it to a ridiculous maximum */
    if (!size)
        size = 0x7fffffff; /* 2.1 billion or so bytes is a lot ;) */

    while (*format && len < size) {
        switch (*format) {
            case '%':
                format++;
                switch (*format) {
                    case 's': /* most popular ;) */
                        s = va_arg(ap, char *);
                        if (s == NULL)
                            s = nullstring;
                        while (*s && len < size)
                            buf[len++] = *s++;
                            format++;
                            break;


                    case 'q':
                        q = 1;
                        /* fallthrough */
                    case 'l':
                        if (*(format + 1) == 'l') {
                            q = 1;
                            format++;
                        }
                        format++;
                        /* fallthrough */

                    case 'd':
                    case 'D':
                    case 'i':
                    case 'o':
                    case 'O':
                    case 'p':
                    case 'u':
                    case 'U':
                    case 'x':
                    case 'X':
                        if (tolower(*format) == 'o') {
                            u = 1; /* octal numbers are always unsigned. */
                            tab = otoa_tab;
                            modulo = 8;
                        } else if (tolower(*format) == 'x' ||
                                *format == 'p') {
                            x = 1;
                            u = 1; /* hex numbers are also always unsigned. */
                            tab = xtoa_tab;
                            modulo = 16;
                            if (*format == 'p') {
                                buf[len++] = '0';
                                if (len < size)
                                    buf[len++] = 'x';
                            }
                        } else {
                            tab = itoa_tab;
                            modulo = 10;
                            if (tolower(*format) == 'u')
                                u = 1;
                        }

                        if (!q) {
                            i = va_arg(ap, unsigned long);
                            i64 = i; /* convert to 64 bit */
                        } else 
                            i64 = va_arg(ap, uint64_t);
                        if (!u && !x)
                            if (i64 & 9223372036854775808ULL) {
                                buf[len++] = '-'; /* it's negative.. */
                                i64 = 9223372036854775808ULL -
                                    (i64 & 9223372036854775808ULL);
                            }
                        s = &num[23];
                        do {
                            *--s = tab[i64 % modulo];
                            i64 /= modulo;
                        } while (i64 != 0);
                        while (*s && len < size)
                            buf[len++] = *s++;
                        format++;
                        q = u = x = 0;
                        break;

                    case 'c':
                        buf[len++] = (char) va_arg(ap, int);
                        format++;
                        break;
                    case '%': /* just an escaped % */
                        buf[len++] = '%';
                        format++;
                        break;
                    default:
                        /* yick, unknown type...default to returning what
                         * our s[n]printf friend would */
#ifndef HAVE_VSNPRINTF
                        /* this is a VERY unfortunate case, hopefully it
                         * won't be run into in the code, but if it is I
                         * have little sympathy.  snprintf is just one of
                         * those things modern systems ought to have, in
                         * any event, simply stick the format in the 
                         * string and move along. (snprintf is also C99 :) */
                        buf[len++] = '%';
                        buf[len++] = format++;
                        break;
#else
                        return vsnprintf(str, size, pattern, ap);
                        break;
#endif
                }
                break;
            default:
                buf[len++] = *format++;
                break;
        }
    }
    buf[len] = 0;
    return len;
}

int ith_sprintf(char *str, const char *format, ...) {
    int ret;
    va_list vl;
    va_start(vl, format);
    ret = ith_vsnprintf(str, 0, format, vl);
    va_end(vl);
    return ret;
}

int ith_snprintf(char *str, size_t size, const char *format, ...) {
    int ret;
    va_list vl;
    va_start(vl, format);
    ret = ith_vsnprintf(str, size, format, vl);
    va_end(vl);
    return ret;
}

int ith_vsprintf(char *str, const char *format, va_list ap) {
    int ret;
    ret = ith_vsnprintf(str, 0, format, ap);
    return ret;
}

/* match code! */
/* instead of setting a maximum call limit, set a maximum depth (12) which
 * should be more than sane for almost any pattern */
#define MAX_MATCH_DEPTH 12
#define mreturn matchdepth--;return

int matchdepth;

/* thanks to lucas for the recursion fixes/optimization! */
static int _match(const char *wild, const char *string) {
    if (++matchdepth > MAX_MATCH_DEPTH) {
        mreturn 0;
    }

    while (1) {
        /* Logic: Match * in a string, this is confusing, sort of
         * if * is the last thing in the wildcard, the match is
         * definite if we've gotten this far.
         * otherwise we try and find every occurance of the
         * the next character in the wildcard within the string
         * and match from there, calling the function recursively
         * until some level below us returns positive, in which
         * case we too return positive.  For strings with lots
         * of wildcards this gets disgustingly recursive.  */
        if (!*wild) {
            mreturn ((*string == '\0') ? 1 : 0);
        }
        if (*wild == '*') {
            wild++;
            /* swallow all extraneous '*'s after.  */
            while (*wild == '*')
                wild++;
            while (*wild == '?' && *string) {
                wild++;
                string++;
            }
            if (!*wild) {
                mreturn 1;
            }
            if (*wild == '*')
                continue;
            while (*string) {
                if (touppertab[(unsigned char)*string] ==
                        touppertab[(unsigned char)*wild] &&
                        _match((wild+1), (string+1))) {
                    mreturn 1;
                }
                string++;
            }
        }
        if (!*string) {
            mreturn 0;
        }
        if (*wild != '?' && touppertab[(unsigned char)*string] !=
            touppertab[(unsigned char)*wild]) {
                mreturn 0;
        }
        string++;
        wild++;
    }
    mreturn 0;
}

int match(const char *wild, const char *string)
{
    matchdepth = 0;
    if (!strcmp(wild, "*"))
        return 1; /* definite match */

    return _match(wild, string);
}

#define MAX_HOSTMATCH_DEPTH 32

/* the below behaves almost exactly like my other match function, with the
* exception that it supports two additional notations which come from
* regular expressions.  the [abcdef...] notation, (and also special
* entries, documented elsehwere), and the (opt1,opt2,opt3,...) notation */
static int _hostmatch(const char *wild, const char *string) {
    if (++matchdepth > MAX_HOSTMATCH_DEPTH) {
        mreturn 0;
    }

    while (1) {
        /* Logic: Match * in a string, this is confusing, sort of
         * if * is the last thing in the wildcard, the match is
         * definite if we've gotten this far.
         * otherwise we try and find every occurance of the
         * the next character in the wildcard within the string
         * and match from there, calling the function recursively
         * until some level below us returns positive, in which
         * case we too return positive.  For strings with lots
         * of wildcards this gets disgustingly recursive.
         */
        if (!*wild) {
            mreturn ((*string == '\0') ? 1 : 0);
        }
        if (*wild == '*') {
            wild++;
            /* swallow all extraneous '*'s after.  */
            while (*wild == '*')
                wild++;
            while (*wild == '?' && *string) {
                wild++;
                string++;
            }
            if (!*wild) {
                mreturn 1;
            }
            if (*wild == '*')
                continue;
            if (*wild == '(' || *wild == '[') {
                while (*string) {
                    if (_hostmatch(wild, string)) {
                        mreturn 1;
                    }
                    string++;
                }
            } else {
                while (*string) {
                    if (touppertab[(unsigned char)*string] ==
                            touppertab[(unsigned char)*wild] &&
                            _hostmatch((wild+1), (string+1))) {
                        mreturn 1;
                    }
                    string++;
                }
            }
        }
        if (!*string) {
            mreturn 0;
        }
        if (*wild == '[') {
            const char *end = wild++;
            /* collator.  support special [:alpha:] and [:number:] semantics,
             * as well as others. */
            while (*end) {
                if (*end == ']')
                    break;
                end++;
            }
            if (!*end) {
                mreturn 0;
            }
            if (!strncasecmp(wild, ":alpha:", 7)) {
                if (isalpha(*string))
                    wild = end + 1;
                else {
                    mreturn 0;
                }
            } else if (!strncasecmp(wild, ":number:", 8)) {
                if (isdigit(*string))
                    wild = end + 1;
                else {
                    mreturn 0;
                }
            }
            if (!strncasecmp(wild, ":alnum:", 7)) {
                if (isalpha(*string) || isdigit(*string))
                    wild = end + 1;
                else {
                    mreturn 0;
                }
            } else {
                while (wild != end) {
                    if (touppertab[(unsigned char)*wild] ==
                            touppertab[(unsigned char)*string]) {
                        wild = end + 1;
                        break;
                    }
                    wild++;
                }
                if (wild == end) {
                    mreturn 0;
                }
            }
            /* if wild != end, we succeeded */
            string++;
            continue;
        }
        /* regretably (?) we do only literal matching here.  but I'm not sure
         * if that's a bad thing.... */
        if (*wild == '(') {
            const char *end = wild++;
            const char *ends = wild;
            while (*end) {
                if (*end == ')')
                    break;
                end++;
            }
            if (!*end) {
                mreturn 0;
            }
            /* we have found the end, now find each substring and do compares */
            while (ends != NULL) {
                if (*ends == '|' || *ends == ')') {
                    if (!strncasecmp(wild, string, ends - wild)) {
                        string += ends - wild;
                        wild = end + 1;
                        break;
                    }
                    wild = ends + 1; 
                    if (*ends == ')') {
                        ends = NULL;
                        break;
                    }
                }
                ends++;
            }
            if (ends == NULL) {
                mreturn 0;
            }
            continue;
        }
        if (*wild != '?' && touppertab[(unsigned char)*string] !=
                touppertab[(unsigned char)*wild]) {
            mreturn 0;
        }
        string++;
        wild++;
    }
    mreturn 0;
}

int hostmatch(const char *wild, const char *string) {
    matchdepth = 0;

    /* in case we get a *, return success immediately */
    if (!strcmp(string, "*"))
        return 1;

    return _hostmatch(wild, string);
}


/* match an IP using one of:
 * regular address (v4/v6)
 * address with significant bits (v4/v6)
 *
 * this is unfortunately not a very cheap call, anymore. */
int ipmatch(const char *wild, const char *str) {
    unsigned char bwild[IPADDR_SIZE]; /* these two are the bit-forms of the */
    unsigned char bstr[IPADDR_SIZE];  /* ... two addresses. */
    unsigned char bitmask[IPADDR_SIZE]; /* we store our bitmap this way because
                                           of the length of IPv6 addresses. */
    int family = PF_INET;
    int alen = 0;
    char addr[IPADDR_MAXLEN + 1];
    int imask = 0;
    int i;
    char *mask;

    memset(bitmask, 0xff, IPADDR_SIZE);
    strlcpy(addr, wild, IPADDR_MAXLEN + 1);

    /* strip off the mask portion if it is there */
    if ((mask = strchr(wild, '/')) != NULL) {
        /* mask form.  mask specifies significant bits from left to right.
         * we must make sure to truncate addr at the point where the mask
         * starts as well, if it starts before IPADDR_MAXLEN. */
        if (IPADDR_MAXLEN > mask - wild)
            addr[mask - wild] = '\0';
        mask++;
    }

    /* now determine the address family */
    family = get_address_type(addr);
    if (family != PF_INET && family != PF_INET6)
        return 0;

    /* and handle the bitmasking if need-be */
    if (mask != NULL) {
        imask = str_conv_int(mask, 0);

        if (family == PF_INET6) {
            if (imask <= 0)
                imask = 128;
            else if (imask > 128)
                imask = 128;
        } else {
            if (imask <= 0)
                imask = 32;
            else if (imask > 32)
                imask = 32;
        }
        memset(bitmask, 0, IPADDR_SIZE);
        /* walk down i, setting bits as necessary. */
        for (i = imask;i > 0;i -= (i % 8 ? i % 8 : 8))
            bitmask[(i - 1) / 8] = 0xff << (i % 8 ? (8 - (i % 8)) : 0);
    }

    /* now copy our addresses into bitmasks.  if either pton fails, assume no
     * match. */
    if (inet_pton(family, addr, bwild) != 1)
        return 0;
    if (inet_pton(family, str, bstr) != 1)
        return 0;

    /* now walk along the wild bitstring and the str bitstring and modify them
     * as per the bitmask string, then do a straight compare. */
    alen = (family == PF_INET6 ? 16 : 4);
    for (i = 0;i < alen;i++) {
        bwild[i] &= bitmask[i];
        bstr[i] &= bitmask[i];

        if (bwild[i] != bstr[i])
            return 0;
    }
    return 1; /* made it all the way through!  voila. */
}

/* these are safe string converters used to, effectively, turn strings into
 * various things.  each is given a default in the case that the string is
 * mangled or somehow unuseable, so it is safe to use these in assignments no
 * matter what. */

/* this function takes a string and determines if it is a boolean value
 * (yes/no, on/off, 1/0).  the second will convert a boolean value to one of
 * the two values given as the second arguments (pos/neg), so for example:
 * bool_conv_str(1, "yes", "no") returns "yes" */
bool str_conv_bool(char *str, bool def) {

    if (str == NULL)
        return def;

    if (!strcasecmp(str, "yes") || !strcasecmp(str, "on") || atoi(str))
        return 1;
    else if (!strcasecmp(str, "no") || !strcasecmp(str, "off") || !atoi(str))
        return 0;

    return def;
}

const char *bool_conv_str(bool val, const char *pos, const char *neg) {

    if (val)
        return pos;
    else
        return neg;
}

/* these two are used to convert from/to a formatted time string.  the string
 * should be of the format [Xd][Xh][Xm][X][s].  e.g. 3600 is 3600s, or 1h.
 * either lower or upper case is acceptable. */
time_t str_conv_time(char *str, int def) {
    char *s = str;
    char *s2;
    time_t ret = 0;

    if (str == NULL)
        return def;

    s2 = strchr(s, 'd');
    if (s2 != NULL) {
        *s2 = '\0';
        ret += str_conv_int(s, 0) * 86400; /* days */
        s = s2 + 1;
    }
    s2 = strchr(s, 'h');
    if (s2 != NULL) {
        *s2 = '\0';
        ret += str_conv_int(s, 0) * 3600; /* hours */
        s = s2 + 1;
    }
    s2 = strchr(s, 'm');
    if (s2 != NULL) {
        *s2 = '\0';
        ret += str_conv_int(s, 0) * 60; /* minutes */
        s = s2 + 1;
    }
    ret += str_conv_int(s, 0); /* seconds */

    return ret;
}

char *time_conv_str(time_t itime) {
    int d, h, m, s;
    static char rbuf[64];
    int rlen = 0;

    d = itime / 86400; /* days */
    itime %= 86400;
    h = itime / 3600; /* hours */
    itime %= 3600;
    m = itime / 60; /* minutes */
    itime %= 60;
    s = itime;

    if (d)
        rlen += sprintf(rbuf + rlen, "%dd", d);
    if (h)
        rlen += sprintf(rbuf + rlen, "%dh", h);
    if (m)
        rlen += sprintf(rbuf + rlen, "%dm", m);
    if (s)
        rlen += sprintf(rbuf + rlen, "%ds", s);
    if (!rlen) { /* if we haven't added anything, it's 0s */
        rbuf[0] = '0';
        rbuf[1] = 's';
        rbuf[2] = '\0';
    }

    return rbuf;
}

/* lastly, wrappers to convert strings to integers and vice versa.  slightly
 * safer than atoi, but less robust than strtol except that we check strtol
 * for errors. :) */
int str_conv_int(char *str, int def) {
    int ret, esave = errno;

    errno = 0;

    if (str == NULL)
        return def;
    ret = strtol(str, NULL, 0);

    if (errno) /* just return their default to them ... */
        ret = def;

    /* XXX: we trample errno here, so the user doesn't know what went wrong.
     * :/ */
    errno = esave;

    return ret;
}

char *int_conv_str(int number) {
    static char rbuf[32];

    sprintf(rbuf, "%d", number); /* this is cheating, but it's easier and not
                                    much slower.. */

    return rbuf;
}

/*****************************************************************************
 * baseN encoding/decoding functions                                         *
 *****************************************************************************/
/* Tables for base conversion.  We use one kind for everything but base64, and
 * another to provide RFC1521 compatible base64 mapping. */
static char base_emap[32] = "0123456789abcdefghijklmnopqrstuv";
static char base64_emap[64] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char base_dmap[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,  0,  1,
     2,  3,  4,  5,  6,  7,  8,  9, -1, -1,
    -1, -1, -1, -1, -1, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, 10, 11, 12,
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 30, 31, -1
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1
};
static char base64_dmap[256] = { /* -2 means skip. */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, /* skip '\t' */
    -2, -2, -2, -2, -1, -1, -1, -1, -1, -1, /* skip '\n', '\v', '\f', '\r' */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -2, -1, -1, -1, -1, -1, -1, -1, /* skip ' ' */
    -1, -1, -1, 62, -1, -1, -1, 63, 52, 53,
    54, 55, 56, 57, 58, 59, 60, 61, -1, -1,
    -1, -2, -1, -1, -1,  0,  1,  2,  3,  4, /* skip '=' */
     5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, -1, -1, -1, -1, -1, -1, 26, 27, 28,
    29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
    39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, -1, -1, -1, -1, -1, -1, -1
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1
};

/* This function encodes a data stream in the specified format into the given
 * buffer.  It is not always safe to encode into the same buffer, so be careful
 * to allocate the necessary space!  The function returns the length of the
 * string, not including the padding NUL byte. */
size_t str_base_encode(char type, char *dest, char *src, size_t len) {
    char *map = (type == BASE64_ENCODING ? base64_emap : base_emap);
    unsigned int chunk = 0;
    int bits = 0;
    size_t count = 0;

    while (len > 0) {
        if (bits < type) {
            chunk <<= 8;
            chunk += (unsigned char)*src++;
            bits += 8;
            len--;
        } 
        while (bits >= type) {
            *dest++ = map[chunk >> (bits - type)];
            bits -= type;
            chunk &= 0xff >> (8 - bits);
            count++;
        }
    }
    if (bits > 0) {
        *dest++ = map[chunk << (type - bits)];
        count++;
    }
    
    *dest = '\0';

    return count;
}

/* This function decodes the stream 'src' (in the specified encoding) into
 * 'dest'.  It is always safe to decode into the same string because the result
 * is always smaller, and will not trample any buffers.  The function returns
 * 'true' if the conversion was successful, and 'false' otherwise.  The
 * conversion will not 'fail' on invalid characters, but it will skip them. */
size_t str_base_decode(char type, char *dest, char *src, size_t len) {
    char *map = (type == BASE64_ENCODING ? base64_dmap : base_dmap);
    unsigned int chunk = 0;
    int bits = 0;
    size_t count = 0;

    errno = 0;
    while (len > 0) {
        if (map[(unsigned char)*src] < 0 ||
                map[(unsigned char)*src] >= (1 << type)) {
            src++;
            len--;
            if (map[(unsigned char)*src] != -2)
                errno = EINVAL;
            continue; /* skip bogus/separator characters */
        }
        chunk <<= type;
        chunk += map[(unsigned char)*src++];
        bits += type;
        len--;
        if (bits >= 8) {
            *dest++ = chunk >> (bits - 8);
            chunk ^= 0xff << (bits - 8);
            bits -= 8;
            count++;
        }
    }
    if (bits != 0) {
        *dest++ = chunk;
        count++;
    }

    return count;
}

/* include fmtcheck if we don't have it */
#ifndef HAVE_FMTCHECK
#include "contrib/fmtcheck.c"
#endif
/* strlcpy and strlcat too */
#ifndef HAVE_STRLCAT
#include "contrib/strlcat.c"
#endif
#ifndef HAVE_STRLCPY
#include "contrib/strlcpy.c"
#endif

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
