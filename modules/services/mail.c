/*
 * mail.c: mail sending system
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file handles the sending of mail.  It creates and maintains a
 * connection to the local mail server, and sends mail as necessary.
 */

#include <ithildin/stand.h>

#include "services.h"

IDSTRING(rcsid, "$Id: mail.c 579 2005-08-21 06:38:18Z wd $");

static struct {
    isocket_t *sock;
    int ehlo;                /* this is set when the EHLO command has been sent */
    char inbuf[512];        /* input buffer */
    int inbytes;        /* bytes of input in the buffer currently */
    char *outbuf;        /* output buffer */
    int outbytes;        /* bytes of output in the buffer. */
} mail;

static void mail_connect(void);
static HOOK_FUNCTION(mail_socket_hook);
static void mail_send_for(regnick_t *);
static void flush_mail_outbuf(void);

/* Create a mail contact.  Copies in the address and adds it to the search
 * listings. */
struct mail_contact *create_mail_contact(char *addr) {
    struct mail_contact *mcp;

    mcp = calloc(1, sizeof(struct mail_contact));
    
    strcpy(mcp->address, addr);

    LIST_INSERT_HEAD(&services.db.list.mail, mcp, lp);
    hash_insert(services.db.hash.mail, mcp);

    return mcp;
}

/* Destroys a mail contact.  This should only be done when a contact has no
 * nickname references. */
void destroy_mail_contact(struct mail_contact *mcp) {

    LIST_REMOVE(mcp, lp);
    hash_delete(services.db.hash.mail, mcp);

    free(mcp);
}

/* Basically we just connect to our mail socket in this function.  It may be
 * called many times, if our connection dies. */
void mail_setup(void) {

    mail_connect();
}

static void mail_connect(void) {

    if (mail.sock != NULL)
        destroy_socket(mail.sock);
    if (mail.outbuf != NULL) {
        free(mail.outbuf);
        mail.outbytes = 0;
    }

    if ((mail.sock = create_socket()) == NULL) {
        log_error("couldn't create mail socket!");
        return;
    }
    if (!set_socket_address(isock_laddr(mail.sock), "0.0.0.0", NULL,
                SOCK_STREAM)) {
        log_error("couldn't set socket address of mail socket!");
        destroy_socket(mail.sock);
        return;
    }
    if (!open_socket(mail.sock)) {
        log_error("couldn't open mail socket.");
        destroy_socket(mail.sock);
        return;
    }
    if (!socket_connect(mail.sock, services.mail.host, services.mail.port,
                SOCK_STREAM)) {
        log_error("couldn't connect to mail server on %s/%s",
                services.mail.host, services.mail.port);
        destroy_socket(mail.sock);
    }

    socket_monitor(mail.sock, SOCKET_FL_READ | SOCKET_FL_WRITE);
    add_hook(mail.sock->datahook, mail_socket_hook);
    log_notice("connected to mail server at %s/%s", services.mail.host,
            services.mail.port);

    mail.ehlo = 0;

    return;
}

/* This function is called in a timer loop to send mail for whatever reasons.
 * Right now only nickname registration mails are sent.  That will probably
 * expand in the future. ;) */
void mail_send(void) {
    regnick_t *np;

    LIST_FOREACH(np, &services.db.list.nicks, lp) {
        if (!regnick_activated(np) && !(np->intflags & NICK_IFL_MAILED))
            mail_send_for(np);
        np->intflags |= NICK_IFL_MAILED;
    }
}

#define MAX_MAIL_BUF 16384
static void mail_send_for(regnick_t *np) {
    static char buf[MAX_MAIL_BUF];
    int idx;
    struct tm *tp;
    char *s;

    /* Start off by putting in the ESMTP commands to actually send the mail */
    idx = snprintf(buf, MAX_MAIL_BUF, "MAIL FROM: %s\nRCPT TO: %s\nDATA\n",
            services.mail.from, np->email->address);
    /* Now add the headers and stuff. */
    tp = localtime(&me.now);
    idx += snprintf(buf + idx, MAX_MAIL_BUF - idx, "Date: ");
    idx += strftime(buf + idx, MAX_MAIL_BUF - idx, "%a, %e %b %Y %T %Z\n", tp);
    idx += snprintf(buf + idx, MAX_MAIL_BUF - idx, "From: %s Services <%s>\n"
            "To: %s <%s>\nSubject: %s registration key for %s\n",
            ircd.network, services.mail.from, np->name, np->email->address,
            ircd.network, np->name);
    /* Now for the ugly part.  We need to add, character by character, the text
     * from the activation template, expanding things as necessary.  We also
     * have to mind the buffer.  Ugh. :) */
    s = services.mail.templates.activate;
    while (*s != '\0' && idx < MAX_MAIL_BUF) {
        switch (*s) {
            case '%':
                s++;
                if (*s == '\0')
                    break;
                switch (*s) {
                    case 'a':
                        idx += snprintf(buf + idx, MAX_MAIL_BUF - idx, "%lld",
                                np->flags);
                        break;
                    case 'n':
                        idx += snprintf(buf + idx, MAX_MAIL_BUF - idx, "%s",
                                ircd.network);
                        break;
                    case 'N':
                        idx += snprintf(buf + idx, MAX_MAIL_BUF - idx, "%s",
                                ircd.network_full);
                        break;
                    case 'p':
                        idx += snprintf(buf + idx, MAX_MAIL_BUF - idx, "%s",
                                np->pass);
                        break;
                    case 'u':
                        idx += snprintf(buf + idx, MAX_MAIL_BUF - idx, "%s",
                                np->name);
                        break;
                    case '%':
                        buf[idx++] = '%';
                        break;
                }
                s++;
            default:
                buf[idx++] = *s++;
                break;
        }
    }

    /* now ensure we terminate the mail.. */
    if (idx > MAX_MAIL_BUF - 3)
        idx = MAX_MAIL_BUF - 3;
    buf[idx++] = '\n';
    buf[idx++] = '.';
    buf[idx++] = '\n';

    /* Last, but not least, append this mail onto the big glop we're storing in
     * the mail structure. */
    mail.outbuf = realloc(mail.outbuf, mail.outbytes + idx);
    memcpy(mail.outbuf, buf, idx);
    mail.outbytes += idx;

    flush_mail_outbuf();
}

HOOK_FUNCTION(mail_socket_hook) {
    char buf[256];
    int len;

    if (SOCKET_WRITE(mail.sock)) {
        if (mail.ehlo == 0) {
            /* we have not sent our greeting yet, do so now. */
            len = sprintf(buf, "EHLO %s\n",
                    strchr(services.mail.from, '@') + 1);
            if (socket_write(mail.sock, buf, len) != len)
                log_warn("could not send entire EHLO message to mail socket!");
            mail.ehlo = 1;
        } else {
            flush_mail_outbuf();
        }
    }

    if (SOCKET_ERROR(mail.sock))
        mail_connect();

    if (SOCKET_READ(mail.sock)) {
        /* Suck in the input data, but for now don't pay attention to it.  We
         * probably should, at some point. ;) */
        while (socket_read(mail.sock, mail.inbuf, 256) != 0)
            log_debug("mail server said: %s", mail.inbuf);
    }

    return NULL;
}

static void flush_mail_outbuf(void) {
    int len;
    char *old = mail.outbuf;

    log_debug("flushing mail output buffer.");

    while ((len = (mail.outbytes < 512 ? mail.outbytes : 512)) != 0) {
        if (socket_write(mail.sock, mail.outbuf, len) != len)
            break; /* can't send anymore data */
        /* otherwise we wrote 'len' bytes, subtract len from outbytes
         * and move outbuf forward appropriately. */
        if ((mail.outbytes -= len) == 0)
            break; /* if we hit 0 we've got nothing to send. */
        mail.outbuf += len;
    }

    /* If we've sent everything, free the old buffer.  If we've sent at
     * least something, allocate it back down. */
    if (mail.outbytes == 0) {
        free(old);
        mail.outbuf = NULL;
    } else if (old != mail.outbuf) {
        mail.outbuf = strdup(mail.outbuf);
        free(old);
    }
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
