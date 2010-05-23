/*
 * string.h: prototypes for string-handling functions.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: string.h 596 2005-10-10 08:48:52Z wd $
 */

#ifndef ISTRING_H
#define ISTRING_H

/* these are tables for converting strings to ASCII upper/lowercase. */
extern unsigned char tolowertab[256];
#undef tolower
#define tolower(c) tolowertab[(unsigned char)(c)]
extern unsigned char touppertab[256];
#undef toupper
#define toupper(c) touppertab[(unsigned char)(c)]

/* re-written str* functions */
char *ith_strdup(const char *str);
int ith_strcasecmp(const char *one, const char *two);
int ith_strncasecmp(const char *one, const char *two, int len);
int ith_strcmp(const char *one, const char *two);
int ith_strncmp(const char *one, const char *two, int len);
char *ith_strcat(char *str, const char *app);
char *ith_strncat(char *str, const char *app, int len);
char *ith_strchr(const char *str, char ch);
char *ith_strrchr(const char *str, char ch);
char *ith_strcpy(char *to, const char *from);
char *ith_strncpy(char *to, const char *from, int len);
size_t ith_strlcat(char *dst, const char *src, size_t siz);
size_t ith_strlcpy(char *dst, const char *src, size_t siz);
size_t ith_strlen(const char *str);
char *ith_strsep(char **from, const char *delim);

#ifndef NO_INTERNAL_LIBC
# undef strcasecmp
# define strcasecmp ith_strcasecmp
# undef strncasecmp
# define strncasecmp ith_strncasecmp
# undef strcmp
# define strcmp ith_strcmp
# undef strncmp
# define strncmp ith_strncmp
# undef strcat
# define strcat ith_strcat
# undef strncat
# define strncat ith_strncat
# undef strchr
# define strchr ith_strchr
# undef strrchr
# define strrchr ith_strrchr
# undef strcpy
# define strcpy ith_strcpy
# undef strncpy
# define strncpy ith_strncpy
# undef strdup
# define strdup ith_strdup
# undef strlen
# define strlen ith_strlen
#endif

#ifndef HAVE_STRLCPY
# define strlcpy ith_strlcpy
#endif
#ifndef HAVE_STRLCAT
# define strlcat ith_strlcat
#endif

#if !defined(NO_INTERNAL_LIBC) || !defined(HAVE_STRSEP)
# undef strsep
# define strsep ith_strsep
#endif


/* re-written *printf functions */
/* this is the real function, others call it */
int ith_vsnprintf(char *str, size_t size, const char *pattern, va_list vl);

int ith_sprintf(char *str, const char *pattern, ...) __PRINTF(2);
int ith_snprintf(char *str, size_t size, const char *pattern, ...) __PRINTF(3);
int ith_vsprintf(char *str, const char *pattern, va_list vl);
#ifndef NO_INTERNAL_LIBC
# undef sprintf
# define sprintf ith_sprintf
# undef snprintf
# define snprintf ith_snprintf
# undef vsprintf
# define vsprintf ith_vsprintf
# undef vsnprintf
# define vsnprintf ith_vsnprintf
#endif

/* matching functions */
int match(const char *wild, const char *string);
int hostmatch(const char *wild, const char *string);
int ipmatch(const char *wild, const char *string);

/* string conversion functions */
bool str_conv_bool(char *, bool);
const char *bool_conv_str(bool, const char *, const char *);
time_t str_conv_time(char *, int);
char *time_conv_str(time_t);
int str_conv_int(char *, int);
char *int_conv_str(int);

/* baseX encoding/decoding/conversion functions.  please note that, in the case
 * of base64, it is *NOT* compatible with RFC1521! */
#define BASE2_ENCODING        1
#define BASE4_ENCODING        2
#define BASE8_ENCODING        3
#define BASE16_ENCODING        4
#define BASE32_ENCODING 5
#define BASE64_ENCODING        6
size_t str_base_encode(char, char *, char *, size_t);
size_t str_base_decode(char, char *, char *, size_t);

/* fmtcheck */
#ifndef HAVE_FMTCHECK
const char *fmtcheck(const char *, const char *);
#endif

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
