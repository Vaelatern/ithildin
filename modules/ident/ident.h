/*
 * ident.h: ident module header for structures/flags
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: ident.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef IDENT_IDENT_H
#define IDENT_IDENT_H

/* timeout in seconds for ident requests */
#define IDENT_TIMEOUT 8

/* maximum length of ident answers */
#define IDENT_MAXLEN 9

struct ident_request {
    struct isock_address laddr;                /* local address of the socket we did
                                           the lookup for */
    struct isock_address raddr;                /* remote address of the socket we did
                                           the lookup for */
    isocket_t *sock;                        /* the socket used to connect to the
                                           auth port. */
    timer_ref_t timer;                        /* the expiration timer */
    char    answer[IDENT_MAXLEN + 1];        /* the answer from the server */
    hook_function_t func;                /* the function to call when the check
                                           is completed. */

    LIST_ENTRY(ident_request) lp;
};

void check_ident(isocket_t *sock, hook_function_t func);
void ident_cancel(hook_function_t func);
#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
