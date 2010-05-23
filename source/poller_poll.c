/*
 * poller_poll.c: poll() polling mechanism
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

IDSTRING(poller_rcsid, "$Id: poller_poll.c 578 2005-08-21 06:37:53Z wd $");

int poll_sockets(time_t timeout) {
    int msec;
    int ret;
    struct isocket *sp;

    if (timeout == 0)
        msec = INFTIM;
    else if ((msec = timeout * 1000) <= 0)
        msec = INT_MAX; /* ... fuh */

    if ((ret = poll(pollfds, maxsockets, msec)) == -1 && errno != EINTR) {
        log_error("poll(%p, %d, %d) error: %s", pollfds, maxsockets,
                msec, strerror(errno));
        return 0;
    } else if (ret <= 0)
        return 1;

    me.now = time(NULL);
    LIST_FOREACH(sp, &allsockets, intlp) {
        if (SOCKET_DEAD(sp) || sp->fd < 0)
            continue; /* dead socket.  don't touch. */
        if (pollfds[sp->fd].revents) {
            if (pollfds[sp->fd].revents & POLLIN)
                sp->state |= SOCKET_FL_READ_PENDING;
            if (pollfds[sp->fd].revents & POLLOUT) {
                sp->state |= SOCKET_FL_WRITE_PENDING;
                socket_unmonitor(sp, SOCKET_FL_WRITE);
            }
            if (pollfds[sp->fd].revents & (POLLERR|POLLHUP|POLLNVAL)) {
                sp->state |= SOCKET_FL_ERROR_PENDING;
                /* attempt a bogus write to get errno.  yech */
                if (write(sp->fd, &sp->state, sizeof(sp->state)) != -1) {
                    log_error("yipes! poll() lied to me!");
                    me.shutdown = 1;
                    return 0;
                }
                sp->err = errno;
            }

        }
        if (sp->state & SOCKET_FL_PENDING)
            socket_event(sp);
    }
        
    return 1;
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
