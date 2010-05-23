#define RFC1459_PKT_LEN 512
#define RFC1459_MAXARGS 15

#ifndef MAX_PACKET_LEN
# define MAX_PACKET_LEN RFC1459_PKT_LEN
#endif

#ifndef MAX_COMMAND_ARGS
# define MAX_COMMAND_ARGS RFC1459_MAXARGS
#endif

const uint64_t protocol_buffer_size = MAX_PACKET_LEN;

HOOK_FUNCTION(input);
struct send_msg *output(struct protocol_sender *, char *, char *, char *,
        va_list);

/* input as much data as we can from the user. */
HOOK_FUNCTION(input) {
    isocket_t *sp = (isocket_t *)data;
    connection_t *cp = (connection_t *)sp->udata;
    int ret = 0;
    char *s;
    bool dirtybuffer;

    /* If we were force-called (ep is NULL) we assume there is data in our
     * buffer.  Jump into the loop. */
    if ((ep == NULL && cp->buflen != 0) || (cp->buflen == cp->bufsize))
        goto loop_entrance;

    while ((ret = socket_read(sp, cp->buf + cp->buflen,
                    cp->bufsize - cp->buflen)) > 0) {
loop_entrance:
        cp->buflen += ret;
        cp->stats.recv += ret;

        while (cp->buflen > 0) {
            dirtybuffer = false;
            /* if the buffer is full but no terminating character (\n) is
               found, we cheat */
            if (cp->buflen == cp->bufsize) {
                /* actually, we might have lots of commands in here, try to
                 * find a \n without overruning the buffer.  If we do, sally
                 * forth undeterred, if we don't, trim the message */
                s = cp->buf;
                while (s < cp->buf + cp->bufsize && *s != '\n')
                    s++;

                /* A command without a newline, add it and note that the
                 * future contents of the buffer are dirty (handled below
                 * when we pass out the data) */
                if (s == cp->buf + cp->bufsize) {
                    cp->buf[cp->bufsize - 1] = '\n';
                    s = cp->buf + cp->bufsize - 1;
                    dirtybuffer = true;
                }
            } else {
                cp->buf[cp->buflen] = '\0';
                s = strchr(cp->buf, '\n');
                if (s == NULL)
                    break; /* no separator found, try reading some more */
            }

            /* null-terminate and get rid of the [\r]\n sequence. */
            if (s > cp->buf && *(s - 1) == '\r')
                *(s - 1) = '\0';
            else
                *s = '\0';

            log_debug("[%s] <%s< %s", ircd.me->name,
                    (cp->cli != NULL ? cp->cli->nick :
                     (cp->srv != NULL ? cp->srv->name : "")), cp->buf);
            /* Don't parse the packet if the buffer is dirty, otherwise do
             * call the parser. */
            if (!(cp->flags & IRCD_CONNFL_DIRTYBUFFER))
            {
                if ((ret = packet_parse(cp)) == IRCD_CONNECTION_CLOSED)
                    return (void *)ret; /* connection closed, stop immediately */
            } else
                cp->flags &= ~IRCD_CONNFL_DIRTYBUFFER;

            s++; /* increment s, now we see if our packet contains more data */
            if (s - cp->buf < cp->buflen) {
                cp->buflen -= s - cp->buf;
                memmove(cp->buf, s, cp->buflen);
                cp->buf[cp->buflen] = '\0';
            } else {
                cp->buflen = 0; /* all done here! */
                *cp->buf = '\0'; /* zero it out */
            }

            /* If the buffer is marked dirty, set the flag now on the
             * connection (since we have passed off the previous command) */
            if (dirtybuffer)
                cp->flags |= IRCD_CONNFL_DIRTYBUFFER;

            if (ret == IRCD_PROTOCOL_CHANGED)
                return (void *)ret;
        }
    }

    /* if there are any errors they will be picked up elsewhere */
    return (void *)0;
}

/* define RFC1459_SEND_MSG_LONG to use the long (nick!user@host) output
 * format. */
struct send_msg *output(struct protocol_sender *from, char *cmd, char *to,
        char *msg, va_list args) {
    static char buf[MAX_PACKET_LEN];
    static struct send_msg sm = {buf, 0};

    if (to != NULL && *to == '\0') /* handle numerics for unreged clients */
        to = "*";

#ifdef RFC1459_SEND_MSG_LONG
    if (from->client != NULL)
        sm.len = sprintf(buf, ":%s!%s@%s %s", from->client->nick,
                from->client->user, from->client->host, cmd);
#else
    if (from->client != NULL)
        sm.len = sprintf(buf, ":%s %s", from->client->nick, cmd);
#endif
    else if (from->server != NULL)
        sm.len = sprintf(buf, ":%s %s", from->server->name, cmd);
    else
        sm.len = sprintf(buf, "%s", cmd);

    /* If to is provided we format the message in a special way to work
     * around some clients incorrect assumptions about the way the server
     * is required to send messages.  Specifially the gaim/pidgin developers
     * refuse to fix a bug in their parser related to handling the JOIN
     * command properly when the last argument is not prefixed with a : (not
     * required). */
    if (to != NULL) {
        if (msg == NULL)
            sm.len += sprintf(buf + sm.len, " :%s", to);
        else
            sm.len += sprintf(buf + sm.len, " %s ", to);
    }

    if (msg != NULL) {
        if (to == NULL)
            /* we need an extra space because of the above.. */
            buf[sm.len++] = ' ';

        sm.len += vsnprintf(&buf[sm.len], MAX_PACKET_LEN - sm.len, msg, args);
    }

    /* XXX: GREAT POTENTIAL FOR EVIL.  Someone running their server in
     * debugmode can see the sent traffic from any server.  Evil, evil,
     * evil! */
    log_debug("[%s] >%s> %s", (from->client != NULL ? from->client->nick :
                (from->server != NULL ? from->server->name : "")), 
            (to != NULL ? to : ""), buf);

    /* Terminate the command */
    if (sm.len > MAX_PACKET_LEN - 2)
        sm.len = MAX_PACKET_LEN - 2;
    buf[sm.len++] = '\r';
    buf[sm.len++] = '\n';

    return &sm;
}

