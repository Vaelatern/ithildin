/*
 * rfc1459.c: the standard client 'RFC1459' protocol
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: rfc1459.c 818 2008-09-21 22:00:54Z wd $");

MODULE_REGISTER("$Rev: 818 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/commands/nick ircd/commands/pass ircd/commands/user
@DEPENDENCIES@: ircd/commands/protocol
*/

/* parser for packets */
static int packet_parse(connection_t *cp);
void setup(connection_t *);

#define RFC1459_SEND_MSG_LONG
#include "shared/rfc1459_io.c"

/* now parse buf.  buf should either be:
 * :prefix COMMAND arg1 arg2 arg3 ... :last arg[\r]\n
 * or:
 * COMMAND arg1 arg2 arg3 ... :last arg[\r]\n */
static int packet_parse(connection_t *cp) {
    char *s, *s2;
    int i;

    /* fill cptr/sptr appropriately */
    cptr.cli = cp->cli;
    sptr = ircd.me;

    s = cp->buf;
    cp->stats.precv++;

    /* devour leading whitespace */
    while (isspace(*s))
        s++;

    if (*s == ':') { /* ignore prefix (what a waste of space...) */
        while (!isspace(*s) && *s)
            s++;
        while (isspace(*s) && *s)
            s++;
    }
        
    if (*s == '\0')
        return 1;
    /* copy maxargs - 1 at most, if there is data left after the loop, copy
     * it into the last argument.  joy */
    for (i = 0;i < RFC1459_MAXARGS - 1;i++) {
        s2 = s;
        if (*s == ':') {
            strncpy(ircd.argv[i], s2 + 1, COMMAND_MAXARGLEN);
            *s = '\0'; /* so we don't trigger below */
            break;
        }
        while (!isspace(*s) && *s)
            s++;
        if (*s != '\0')
            *s++ = '\0';
        strncpy(ircd.argv[i], s2, COMMAND_MAXARGLEN);
        while (isspace(*s) && *s)
            s++;
        if (*s == '\0')
            break;
    }
    /* only copy if there is more data and we fell out of the loop */
    if (*s && i == RFC1459_MAXARGS - 1)
        strncpy(ircd.argv[i], s, COMMAND_MAXARGLEN);

    ircd.argc = i + 1;
    return command_exec_client(ircd.argc, ircd.argv, cptr.cli);
}

/* create a client space, make sure they point back to us. */
void setup(connection_t *cp) {

    if (cp->buf == NULL) {
        cp->buf = malloc(RFC1459_PKT_LEN);
        cp->buflen = 0;
        cp->bufsize = RFC1459_PKT_LEN;
        memset(cp->buf, 0, cp->bufsize);
    }

    create_client(cp);
    cp->cli->server = ircd.me;
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
