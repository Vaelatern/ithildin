/*
 * poller_kqueue.c: kernel queue polling mechanism
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

IDSTRING(poller_rcsid, "$Id: poller_kqueue.c 578 2005-08-21 06:37:53Z wd $");

int poll_sockets(time_t timeout) {
    struct timespec tv = {timeout, 0};
    struct kevent *ke = kev_list;
    int ret = kevent(kqueuefd, kev_change, kev_num_changes, ke,
            (signed int)maxsockets * 2, (timeout ? &tv : NULL));
    struct isocket *sp;

    if (ret == -1 && errno != EINTR) {
        log_error("kevent(%d, %p, %d, %p, %d, %p) error: %s", kqueuefd,
                kev_change, kev_num_changes, ke, maxsockets * 2, &tv,
                strerror(errno));
        return 0;
    } else if (ret <= 0)
        return 1; /* nothing to do, ho-hum */

    kev_num_changes = 0; /* changes have been made.  nifty */

    me.now = time(NULL);
    for (ret--;ret > -1;ret--) {
        if (ke[ret].flags & EV_ERROR)
            log_debug("kevent error on %d/%d: %s", ke[ret].ident,
                    ke[ret].filter, strerror(ke[ret].data));
        else if (ke[ret].udata != NULL) {
            sp = ke[ret].udata;
            if (ke[ret].filter == EVFILT_READ) {
                if (ke[ret].flags & EV_EOF) {
                    sp->state |= SOCKET_FL_ERROR_PENDING;
                    sp->err = ke[ret].fflags;
                }
                sp->state |= SOCKET_FL_READ_PENDING;
            } else if (ke[ret].filter == EVFILT_WRITE) {
                if (ke[ret].flags & EV_EOF) {
                    sp->state |= SOCKET_FL_ERROR_PENDING;
                    sp->err = ke[ret].fflags;
                }
                else {
                    sp->state |= SOCKET_FL_WRITE_PENDING;
                    socket_unmonitor(sp, SOCKET_FL_WRITE);
                }
            } else
                log_warn("unknown kqueue filter %d given!", ke[ret].filter);
        }
    }

    LIST_FOREACH(sp, &allsockets, intlp) {
        if (SOCKET_DEAD(sp) || sp->fd < 0)
            continue; /* dead socket. */

        if (sp->state & SOCKET_FL_PENDING)
            socket_event(sp);
    }

    return 1;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
