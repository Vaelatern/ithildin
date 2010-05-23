/*
 * poller_select.c: select() polling mechanis,
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

IDSTRING(poller_rcsid, "$Id: poller_select.c 578 2005-08-21 06:37:53Z wd $");

int poll_sockets(time_t timeout) {
    fd_set rfds, wfds;
    struct timeval tv = {timeout, 0}; /* sleep at most 50ms */
    struct isocket *sp;
    int ret;

    memcpy(&rfds, &select_rfds, sizeof(fd_set));
    memcpy(&wfds, &select_wfds, sizeof(fd_set));

    if ((ret = select(maxsockets, &rfds, &wfds, NULL,
                    (timeout ? &tv : NULL))) == -1 && errno != EINTR) {
        log_error("select(%d, %p, %p, NULL, %p) error: %s", maxsockets, &rfds,
                &wfds, &tv, strerror(errno));
        return 0;
    } else if (ret <= 0)
        return 1; /* nothing to do, but nothing wrong */

    me.now = time(NULL);
    LIST_FOREACH(sp, &allsockets, intlp) {
        if (SOCKET_DEAD(sp) || sp->fd < 0)
            continue; /* dead socket. */
        if (FD_ISSET(sp->fd, &rfds))
            sp->state |= SOCKET_FL_READ_PENDING;
        if (FD_ISSET(sp->fd, &wfds)) {
            sp->state |= SOCKET_FL_WRITE_PENDING;
            socket_unmonitor(sp, SOCKET_FL_WRITE);
        }
        if (sp->state & SOCKET_FL_PENDING)
            socket_event(sp);
    }
        
    return 1;
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
