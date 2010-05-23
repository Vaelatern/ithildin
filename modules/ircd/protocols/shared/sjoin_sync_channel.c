/*
 * Copyright (C) 2003 the Ithildin Project
 *
 * This function is used identically in at least the bahamut14 and ithildin1
 * protocols.  As such I just placed it in this small .c file and included it
 * from both of them (with BUF_SIZE defined as necessary in the respective
 * files with respect to protocol line lengths)
 */

/* syncing channels is quite the pain as well.  this routine is really bloated
 * and complicated, mainly because channels just have a lot of data. */
void sync_channel(connection_t *conn, channel_t *chan) {
    struct chanlink *clp;
    char modes[64], *m;
    char buf[BUF_SIZE]; /* hope this doesn't get overrun ;) */
    unsigned char *s;
    int optused;
    int len, cnt;
    int sjs; /* set to 1 when we've sent the initial (moded) SJOIN for each
                channel. */
    void *state; /* state for chanmode_query */

    /* send an SJOIN for each channel.  this can be very irritating for
     * large channels as we may need to fill many buffers.  yuck.  Our
     * buffer size is determined by channel name length and some other
     * goop, and should be (I hope) protocol safe. :/.  Also, we have to
     * make sure we only send for nicks that are on *our* side.  if we've
     * parsed SJOINs before, we can't verywell be sending back their users!
     * so we borrow the *_uplink functions from send.c to see where each
     * person comes from. */

    sjs = 0;
    len = 0;
    LIST_FOREACH(clp, &chan->users, lpchan) {
        if (cli_uplink(clp->cli) == conn)
            continue; /* move along */

        /* the '20' is the number of possible prefixes (16) plus some extra
         * space for spaces and junk */
        if (BUF_SIZE - len <= ircd.limits.nicklen + 20) {
            /* if our buffer might be full, send this off. */
            if (sjs)
                sendto_serv(conn->srv, "SJOIN", "%d %s + :%s", chan->created,
                        chan->name, buf);
            else {
                sendto_serv(conn->srv, "SJOIN", "%d %s + :%s", chan->created,
                        chan->name, buf);
                sjs = 1;
            }

            len = 0;
        }
        len += sprintf(&buf[len], "%s%s ",
                chanmode_getprefixes(chan, clp->cli), clp->cli->nick);
    }
    /* leftovers? do the send here too */
    if (len) {
        if (sjs)
            sendto_serv(conn->srv, "SJOIN", "%d %s + :%s", chan->created,
                    chan->name, buf);
        else {
            sendto_serv(conn->srv, "SJOIN", "%d %s + :%s", chan->created,
                    chan->name, buf);
            sjs = 1;
        }
    }
    /* now send modes.  this code is based somewhat on the reset code in
     * commands/mode.c */
    s = ircd.cmodes.avail;
    m = modes;
    len = cnt = 0;
    *m++ = '+';
    *buf = '\0';
    while (*s) {
        state = NULL;
        if (ircd.cmodes.modes[*s].flags & CHANMODE_FL_PREFIX) {
            s++;
            continue; /* skip ... */
        }
        optused = BUF_SIZE - len - 2;
        while (chanmode_query(*s, chan, buf + len, &optused, &state) == CHANMODE_OK) {
            if (optused < 0) {
                /* no space.. send what we have and try again */
                *m = '\0';
                sendto_serv(conn->srv, "MODE", "%s %d %s %s", chan->name,
                        chan->created, modes, buf);
                m = modes + 1;
                len = cnt = 0;
                *buf = '\0';
                optused = BUF_SIZE - len - 2;
                continue;
            }
            *m++ = *s;
            cnt++;
            if (optused) {
                len += optused;
                buf[len++] = ' '; /* append a space */
                buf[len] = '\0';
            }
            if (cnt == 6) {
                *m = '\0';
                sendto_serv(conn->srv, "MODE", "%s %d %s %s", chan->name,
                        chan->created, modes, buf);
                m = modes + 1;
                len = cnt = 0;
                *buf = '\0';
                optused = BUF_SIZE - len - 2;
            }
            optused = BUF_SIZE - len - 2;
        }
        s++;
    }
    /* send off any spares. */
    if (cnt) {
        *m = '\0';
        sendto_serv(conn->srv, "MODE", "%s %d %s %s", chan->name,
                chan->created, modes, buf);
    }
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
