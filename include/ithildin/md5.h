/* MD5.H - header file for MD5C.C
 * $Id: md5.h 578 2005-08-21 06:37:53Z wd $
 */

/* Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
rights reserved.

License to copy and use this software is granted provided that it
is identified as the "RSA Data Security, Inc. MD5 Message-Digest
Algorithm" in all material mentioning or referencing this software
or this function.

License is also granted to make and use derivative works provided
that such works are identified as "derived from the RSA Data
Security, Inc. MD5 Message-Digest Algorithm" in all material
mentioning or referencing the derived work.

RSA Data Security, Inc. makes no representations concerning either
the merchantability of this software or the suitability of this
software for any particular purpose. It is provided "as is"
without express or implied warranty of any kind.

These notices must be retained in any copies of any part of this
documentation and/or software.
 */

#ifndef MD5_H
#define MD5_H
/* MD5 context. */
typedef struct ith_md5context {
  uint32_t state[4];        /* state (ABCD) */
  uint32_t count[2];        /* number of bits, modulo 2^64 (lsb first) */
  unsigned char buffer[64];        /* input buffer */
} ith_md5_ctx;


void md5_init(ith_md5_ctx *);
void md5_update(ith_md5_ctx *, const unsigned char *, unsigned int);
void md5_pad(ith_md5_ctx *);
void md5_final(unsigned char [16], ith_md5_ctx *);
char *md5_end(ith_md5_ctx *, char *);
char *md5_file(const char *, char *);
char *md5_data(const unsigned char *data, unsigned int len, char *buf);
char * MD5Data(const unsigned char *, unsigned int, char *);
#endif /* MD5_H */
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
