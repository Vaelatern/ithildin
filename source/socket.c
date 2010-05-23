/*
 * socket.c: socket handling support functions.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#ifdef HAVE_OPENSSL
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

IDSTRING(rcsid, "$Id: socket.c 779 2006-10-02 00:56:39Z wd $");

/* if the system has no getaddrinfo call, use gailib in contrib */
#ifndef HAVE_GETADDRINFO
#include "contrib/gailib.c"
#endif

static int socket_setflags(int fd);
static inline void socket_event(isocket_t *);
HOOK_FUNCTION(adjust_maxsockets);

unsigned int maxsockets = 1024; /* default is for 1024 sockets maximum */
unsigned int cursockets = 0; /* current number of sockets open */
struct isocket_list allsockets;

#if defined(POLLER_SELECT)
fd_set select_rfds, select_wfds;
#elif defined(POLLER_POLL)
struct pollfd *pollfds = NULL;
#elif defined(POLLER_KQUEUE)
int kqueuefd;
struct kevent *kev_list = NULL, *kev_change = NULL;
int kev_num_changes;
#endif

/* hints structure.  pretty meager, except that we assume we're going to
 * listen() by default */
struct addrinfo gai_hint;

void init_socketsystem(void) {

    /* add our adjustment function to the read_conf event, then call it */
    add_hook(me.events.read_conf, adjust_maxsockets);
    adjust_maxsockets(NULL, NULL);

    /* we do this here, and only here... */
#if defined(POLLER_SOCKET)
    FD_ZERO(&select_rfds);
    FD_ZERO(&select_wfds);
#elif defined(POLLER_KQUEUE)
    kqueuefd = kqueue();
    if (kqueuefd == -1) {
        printf("error creating kqueue fd: %s\n", strerror(errno));
        exit(1);
    }
#endif

    gai_hint.ai_flags = AI_PASSIVE;
    gai_hint.ai_family = PF_UNSPEC;

#ifdef HAVE_OPENSSL
    if (me.ssl.enabled && (me.ssl.ctx = create_ssl_context()) == NULL) {
        log_error("could not create default SSL context.  ssl is disabled.");
        me.ssl.enabled = 0;
    }
#endif
}

HOOK_FUNCTION(adjust_maxsockets) {
    unsigned long oldmax = maxsockets;
#if defined(POLLER_POLL)
    int i;
#endif
    char *s = conf_find_entry("maxsockets", me.confhead, 1);

    if (s != NULL) {
        unsigned long l = str_conv_int(s, 0);
        if (l <= 0) {
            log_error("Inapproriate value for maxsockets (%s) ignored.", s);
            return NULL;
        }
        else if (l == maxsockets && ep != NULL)
            return NULL; /* silently ignore this unless we're being called for
                            purposes of initialization (ep will be NULL) */
        else {
            maxsockets = l;
            log_notice("Adjusting maximum open sockets to %ld", l);
        }
    }

    /* if we were called with double NULLs, we are initializing, otherwise
     * this function was called as a result of a configuration re-read */
    if (ep == NULL && data == NULL)
        oldmax = 0;

    if (oldmax > maxsockets) {
        if (cursockets > maxsockets)
            log_notice("Current open sockets (%d) are greater than "
                    "maxsockets (%d), no connections will be closed.",
                    cursockets, maxsockets);
        /* no work needs done. */
        return 0;
    }

    /* we can assume that oldmax is set to one more than the last valid item
     * in either array (or 0), and can therefore initialize all items past
     * that point. */
#if defined(POLLER_POLL)
    pollfds = realloc(pollfds, sizeof(struct pollfd) * maxsockets);
    for (i = oldmax;i < maxsockets;i++) {
        pollfds[i].fd = -1;
        pollfds[i].events = pollfds[i].revents = 0;
    }
#elif defined(POLLER_KQUEUE)
    kev_list = realloc(kev_list, sizeof(struct kevent) * maxsockets * 2);
    kev_change = realloc(kev_change, sizeof(struct kevent) * maxsockets);
#endif

    return 0;
}

/* this creates a new socket structure, making it completely empty.  the socket
 * must then be bound and opened, using the functions below. */
isocket_t *create_socket(void) {
    isocket_t *sock = NULL;

    sock = malloc(sizeof(isocket_t));
    /* only one hook allowed per socket */
    sock->datahook = create_event(EVENT_FL_ONEHOOK|EVENT_FL_NORETURN);
    sock->state = sock->err = 0;
    sock->udata = NULL; /* only place udata is ever touched */
    sock->sockaddr.addr = sock->peeraddr.addr = NULL;
    sock->sockaddr.family = sock->peeraddr.family = PF_UNSPEC;
    sock->fd = -1;
#ifdef HAVE_OPENSSL
    sock->ssl = NULL;
    sock->ssl_start = 0;
#endif

#ifdef DEBUG_CODE
    /* do a little sanity check to make sure the socket isn't already in our
     * list.  this is very expensive, and theoretically shouldn't ever be used
     * elsewhere, however bugs have been turning up where the socket list is
     * utterly corrupted, so here we go. */
    {
        isocket_t *sp;
        LIST_FOREACH(sp, &allsockets, intlp) {
            if (sp == sock) {
                log_error("socket %p(%d) is already in our socket list!",
                        sock, sock->fd);
                exit_process(NULL, NULL);
            }
        }
    }
#endif
    LIST_INSERT_HEAD(&allsockets, sock, intlp);
    return sock;
}

/* This functions destroys the passed socket structure, and closes it if
 * necessary.  It actually only marks the socket as dead so that it can be
 * safely reaped outside of a polling cycle. */
int destroy_socket(isocket_t *sock) {
    if (sock == NULL)
        return 0;

    /* we only close the socket and mark it as dead, we reap dead sockets after
     * the polling phase.  see reap_dead_sockets() */
    close_socket(sock);
    sock->state |= SOCKET_FL_DEAD;

    return 1; /* always successful */
}

/* This function 'opens' a socket.  This means that it binds the socket to an
 * address, allocates an fd for use, and sets the socket nonblocking. */
int open_socket(isocket_t *sock) {
    if (sock == NULL || sock->sockaddr.addr == NULL ||
            sock->state & SOCKET_FL_OPEN)
        return 0;

    if (cursockets >= maxsockets) {
        log_debug("attempt to open new fd denied, cursockets >= maxsockets");
        return 0;
    }
    sock->fd = socket(sock->sockaddr.family, sock->sockaddr.type,
            sock->sockaddr.protocol);
    if (sock->fd == -1) {
        log_error("socket() failed: %s", strerror(errno));
        sock->err = errno;
        return 0;
    }
        
    if (!socket_setflags(sock->fd)) {
        close(sock->fd);
        return 0;
    }

    if (bind(sock->fd, sock->sockaddr.addr, sock->sockaddr.addrlen)) {
        char host[FQDN_MAXLEN];
        int port;
        get_socket_address(isock_laddr(sock), host, FQDN_MAXLEN, &port);
        log_error("bind(%s/%d) failed: %s", host, port, strerror(errno));
        sock->err = errno;
        return 0;
    }

    sock->state |= SOCKET_FL_OPEN;
    cursockets++;
    return 1;
}

/* This function closes a socket, and removes any monitoring that may be set on
 * the socket. */
int close_socket(isocket_t *sock) {
    if (sock == NULL || !(sock->state & SOCKET_FL_OPEN))
        return 0;

#ifdef HAVE_OPENSSL
    if (sock->ssl != NULL) {
        SSL_shutdown(sock->ssl);
        SSL_free(sock->ssl);
        sock->ssl = NULL;
    }
#endif

    if (close(sock->fd)) {
        log_error("close() failed: %s", strerror(errno));
        sock->err = errno;
        return 0;
    }

    sock->state &= ~SOCKET_FL_OPEN;
    cursockets--;

    /* any events associated with this socket will be deleted automagically
     * when it is no longer valid at the next call to kevent, so don't bother
     * doing it */
#ifndef POLLER_KQUEUE
    socket_unmonitor(sock, SOCKET_FL_PENDING); /* turn it all off */
#endif

    sock->fd = -1;
    return 1;
}

/* This is a wrapper around getaddrinfo which we use to set various bits for
 * our socket.  This is a one-off call!  If you call it again, the previous
 * data will be blown away.  We only use the first item returned from
 * getaddrinfo, and there should probably be a way to set preferences for this.
 * Oh well. */
int set_socket_address(struct isock_address *addr, char *host, char *port,
        int type) {
    struct addrinfo *ai;
    int error;

    if (addr == NULL)
        return 0;

    if (addr->addr != NULL) {
        /* blow away old data */
        free(addr->addr);
    }

    gai_hint.ai_socktype = type;
    if ((error = getaddrinfo(host, port, &gai_hint, &ai))) {
        log_warn("getaddrinfo(%s, %s): %s", host, port, gai_strerror(error));
        return 0;
    }

    if (addr->addr != NULL)
        free(addr->addr);
    /* copy over necessary stuff.  allocate space for the sockaddr, too. */
    addr->addr = malloc(ai->ai_addrlen);
    memcpy(addr->addr, ai->ai_addr, ai->ai_addrlen);
    addr->addrlen = ai->ai_addrlen;
    addr->family = ai->ai_family;
    addr->type = ai->ai_socktype;
    addr->protocol = ai->ai_protocol;

    freeaddrinfo(ai);
    return 1;
}

/* This function gets the address data from an 'isock_address' structure and
 * returns it in the form of a host (never looked up) and an integer port. */
int get_socket_address(struct isock_address *addr, char *host, size_t hlen,
        int *port) {
    char shost[NI_MAXHOST + 1];
    char sport[NI_MAXSERV + 1];
    int error;

    if (addr == NULL)
        return 0;

    if ((error = getnameinfo(addr->addr, (socklen_t)addr->addrlen,
                    shost, NI_MAXHOST, sport, NI_MAXSERV,
                    NI_NUMERICHOST|NI_NUMERICSERV))) {
        log_warn("getnameinfo(): %s", gai_strerror(error));
        return 0;
    }

    if (host != NULL)
        strlcpy(host, shost, hlen);
    if (port != NULL)
        *port = str_conv_int(sport, 0);
    return 1;
}

/* This function is a light wrapper for the system listen() call, and should be
 * used only after a socket is bound and opened. */
int socket_listen(isocket_t *sock) {
    if (sock == NULL || !(sock->state & SOCKET_FL_OPEN))
        return 0;

    if (listen(sock->fd, 128)) {
        log_error("listen() failed: %s", strerror(errno));
        sock->err = errno;
        return 0;
    }

    sock->state |= SOCKET_FL_LISTENING;
    return 1;
}

/* This function is used in conjunction with listening sockets to accept new
 * connections.  If a connection is available on the socket, a new socket
 * structure is allocated and filled in and passed back to the caller.  This
 * function may be called repeatedly to exhaust the list of available incoming
 * connections on a socket. */
isocket_t *socket_accept(isocket_t *sock) {
    isocket_t *s;
    size_t alen = 0;
    int fd = -1;
    struct sockaddr *sa;

    if (sock == NULL || !(sock->state & SOCKET_FL_LISTENING))
        return NULL;

    sa = malloc(sock->sockaddr.addrlen); 
    if (cursockets >= maxsockets) {
        log_debug("attempt to accept() a connection when cursockets >= maxsockets");
        /* try and accept the connection anyhow, and close it.  it might be
         * worthwhile to stop listening on this til cursockets drops down below
         * maxsockets.  Let's remember that for later. ;) */
        while ((fd = accept(sock->fd, sa, &alen)) > -1)
            close(fd); /* heh.. */
        free(sa);
        return NULL;
    }

    /* allocate space for our sockaddr. */
    alen = sock->sockaddr.addrlen;
    if ((fd = accept(sock->fd, sa, &alen)) < 0) {
        if (errno != EWOULDBLOCK) {
            log_error("accept() failed: %s", strerror(errno));
            sock->err = errno;
        }
        free(sa);
        return NULL;
    }
    s = create_socket();
    s->fd = fd;
    s->state |= SOCKET_FL_OPEN;
    if (!socket_setflags(s->fd)) {
        destroy_socket(s);
        free(sa);
        return NULL;
    }
    s->state |= SOCKET_FL_CONNECTED;
    cursockets++;
        
    /* fill in our peer address. */
    s->peeraddr.addr = sa;
    s->peeraddr.addrlen = alen;
    s->peeraddr.family = sock->sockaddr.family;
    s->peeraddr.type = sock->sockaddr.type;
    s->peeraddr.protocol = sock->sockaddr.protocol;
    /* fill in our local address, too. */
    sa = malloc(alen);
    if (getsockname(s->fd, sa, &alen)) {
        log_error("getsockname(%d) failed: %s", s->fd, strerror(errno));
        s->err = errno;
    }
    s->sockaddr.addr = sa;
    s->sockaddr.addrlen = alen;
    s->sockaddr.family = sock->sockaddr.family;
    s->sockaddr.type = sock->sockaddr.type;
    s->sockaddr.protocol = sock->sockaddr.protocol;

    return s;
}

/* connect to the specified address (requires a previously created socket),
 * it is recommended the address be in  form that does not require lookups,
 * since the address lookups here are not non-blocking. */
int socket_connect(isocket_t *sock, char *host, char *port, int type) {

    if (sock == NULL || sock->state & SOCKET_FL_CONNECTED)
        return 0;

    /* use gai_hint from set_socket_address. */
    gai_hint.ai_flags = 0;
    if (!set_socket_address(&sock->peeraddr, host, port, type)) {
        log_error("socket_connect(%s, %s): failed to get remote address", host,
                port);
        return 0;
    }
    gai_hint.ai_flags = AI_PASSIVE; /* re-set this for regular calls. */

    if (connect(sock->fd, sock->peeraddr.addr, sock->peeraddr.addrlen) &&
            errno != EINPROGRESS) {
        /* ECONNREFUSED is not a very interesting error, but others might be.
         * log ECONNREFUSED as debug. */
        log_msg((errno != ECONNREFUSED ? LOGTYPE_ERROR : LOGTYPE_DEBUG),
                "connect() failed: %s", strerror(errno));
        sock->err = errno;
        return 0;
    }

    sock->state |= SOCKET_FL_CONNECTED;
    return 1;
}

/* Linux handily supports a MSG_NOSIGNAL flag for recv, so if this is defined,
 * use it as the only flag to recv(). */
#ifdef MSG_NOSIGNAL
# define SOCKET_RECV_FLAGS MSG_NOSIGNAL
# define SOCKET_SEND_FLAGS MSG_NOSIGNAL
#else
# define SOCKET_RECV_FLAGS 0
# define SOCKET_SEND_FLAGS 0
#endif

/* These two functions are used to read data from, and write data to a socket,
 * respectively.  They return the amount of data actually read, and should be
 * passed a buffer with enough space to hold nbytes of data.  No more than
 * 'nbytes' will be read.  Since sockets are non-blocking, any 'try again'
 * conditions are caught by the functions, which will return 0 if no data could
 * be sent or no data is availble. */
int socket_read(isocket_t *sock, char *buf, size_t nbytes) {
    int ret;

    if (!(sock->state & SOCKET_FL_OPEN))
        return -1;

    /* in case something stupid happens ;) */
    assert(nbytes > 0 && nbytes < INT_MAX);

    errno = 0;
#ifdef HAVE_OPENSSL
    if (SOCKET_SSL(sock))
        ret = SSL_read(sock->ssl, buf, nbytes);
    else
#endif
#ifdef HAVE_RECV
        ret = recv(sock->fd, buf, nbytes, SOCKET_RECV_FLAGS);
#else
        ret = read(sock->fd, buf, nbytes);
#endif

#ifdef HAVE_OPENSSL
    if (SOCKET_SSL(sock))
        switch (SSL_get_error(sock->ssl, ret)) {
            case SSL_ERROR_WANT_READ:
                sock->state |= SOCKET_FL_SSLWANT_READ;
                socket_monitor(sock, SOCKET_FL_READ | SOCKET_FL_INTERNAL);
                return 0;
            case SSL_ERROR_WANT_WRITE:
                sock->state |= SOCKET_FL_SSLWANT_WRITE;
                socket_monitor(sock, SOCKET_FL_WRITE | SOCKET_FL_INTERNAL);
                return 0;
            case SSL_ERROR_NONE:
                break; /* ignore this case */
            case SSL_ERROR_SYSCALL:
                /* log this to debug */
                if (ERR_peek_error() == 0) {
                    if (ret == 0)
                        log_debug("socket_read(SSL%d, %p, %d): "
                                "invalid eof", sock->fd, buf, nbytes);
                    else
                        log_debug("socket_read(SSL%d, %p, %d): %s", sock->fd,
                                buf, nbytes, strerror(errno));
                } else
                    log_debug("socket_read(SSL%d, %p, %d): %s", sock->fd, buf,
                            nbytes, ERR_error_string(ERR_get_error(), NULL));
                sock->state |= SOCKET_FL_ERROR_PENDING;
                return -1;
            case SSL_ERROR_SSL:
                /* This is a pretty nasty case. */
                log_error("socket_read(SSL%d, %p, %d): %s", sock->fd, buf,
                        nbytes, ERR_error_string(ERR_get_error(), NULL));
                sock->state |= SOCKET_FL_ERROR_PENDING;
                return -1;
            default:
                sock->state |= SOCKET_FL_ERROR_PENDING;
                return -1;
        }
    else
#endif
    if (ret < 0) {
        switch (errno) {
            case EAGAIN:
            case EINTR:
                return 0; /* ignore this */
            default:
                sock->state |= SOCKET_FL_ERROR_PENDING;
                sock->err = errno;
                return -1;
        }
    } else if (ret == 0) {
        /* EOF received (usually (always?) errno will not be set) */
        sock->state |= SOCKET_FL_ERROR_PENDING;
        if (errno != 0)
            sock->err = errno;
        else
            sock->state |= SOCKET_FL_EOF;
        return -1;
    }

    return ret;
}
int socket_write(isocket_t *sock, char *buf, size_t nbytes) {
    int ret;

    if (!(sock->state & SOCKET_FL_OPEN))
        return -1;

    assert(nbytes > 0 && nbytes < INT_MAX);

    errno = 0;
#ifdef HAVE_OPENSSL
    if (SOCKET_SSL(sock))
        ret = SSL_write(sock->ssl, buf, nbytes);
    else
#endif
#ifdef HAVE_SEND
        ret = send(sock->fd, buf, nbytes, SOCKET_SEND_FLAGS);
#else
        ret = write(sock->fd, buf, nbytes);
#endif

#ifdef HAVE_OPENSSL
    if (SOCKET_SSL(sock))
        switch (SSL_get_error(sock->ssl, ret)) {
            case SSL_ERROR_WANT_READ:
                sock->state |= SOCKET_FL_SSLWANT_READ;
                socket_monitor(sock, SOCKET_FL_READ | SOCKET_FL_INTERNAL);
                return 0;
            case SSL_ERROR_WANT_WRITE:
                sock->state |= SOCKET_FL_SSLWANT_WRITE;
                socket_monitor(sock, SOCKET_FL_WRITE | SOCKET_FL_INTERNAL);
                return 0;
            case SSL_ERROR_NONE:
                break; /* ignore this case */
            case SSL_ERROR_SYSCALL:
                /* log this to debug */
                if (ERR_peek_error() == 0) {
                    if (ret == 0)
                        log_debug("socket_write(SSL%d, %p, %d): "
                                "invalid eof", sock->fd, buf, nbytes);
                    else
                        log_debug("socket_write(SSL%d, %p, %d): %s", sock->fd,
                                buf, nbytes, strerror(errno));
                } else
                    log_debug("socket_write(SSL%d, %p, %d): %s", sock->fd, buf,
                            nbytes, ERR_error_string(ERR_get_error(), NULL));
                sock->state |= SOCKET_FL_ERROR_PENDING;
                return -1;
            case SSL_ERROR_SSL:
                /* This is a pretty nasty case. */
                log_error("socket_write(SSL%d, %p, %d): %s", sock->fd, buf,
                        nbytes, ERR_error_string(ERR_get_error(), NULL));
                sock->state |= SOCKET_FL_ERROR_PENDING;
                return -1;
            default:
                sock->state |= SOCKET_FL_ERROR_PENDING;
                return -1;
        }
    else
#endif
    if (ret == -1) {
        switch (errno) {
            case EAGAIN:
            case EINTR:
                sock->state &= ~SOCKET_FL_WRITE_PENDING;
                if (sock->state & SOCKET_FL_WANT_WRITE)
                    socket_monitor(sock, SOCKET_FL_WRITE);
                return 0;
            default:
                sock->state |= SOCKET_FL_ERROR_PENDING;
                sock->err = errno;
                return -1;
        }
    }

    if ((size_t)ret != nbytes && sock->state & SOCKET_FL_WANT_WRITE) {
        sock->state &= ~SOCKET_FL_WRITE_PENDING;
        socket_monitor(sock, SOCKET_FL_WRITE);
    }
    return ret;
}

/* a little function which will return the error condition of a socket.  Unless
 * the socket has an internal error (currently just EOF) we simply return
 * strerror on the socket's errno value */
const char *socket_strerror(isocket_t *sock) {

    if (sock->state & SOCKET_FL_EOF)
        return "Received EOF from peer";
    else
        return strerror(sock->err);
}

/* these two functions tell the polling system to either watch or stop watching
 * for specific events on the socket.  The available events are currently only
 * reading or writing (error conditions are always monitored).  It is necessary
 * to set a hook on the socket's data event and to add some kind of monitoring
 * to the socket in order to get hooks called for socket data events. */
void socket_monitor(isocket_t *sock, int mask) {
#if defined(POLLER_KQUEUE)
    struct kevent *ke = kev_change + kev_num_changes;
#endif

    /* If this wasn't an internal call set the want conditions so we know what
     * kind of monitoring the user wants. */
    if (!(mask & SOCKET_FL_INTERNAL)) {
        if (mask & SOCKET_FL_READ)
            sock->state |= SOCKET_FL_WANT_READ;
        if (mask & SOCKET_FL_WRITE)
            sock->state |= SOCKET_FL_WANT_WRITE;
    }

#if defined(POLLER_SELECT)
    if (mask & SOCKET_FL_READ)
        FD_SET(sock->fd, &select_rfds);
    if (mask & SOCKET_FL_WRITE)
        FD_SET(sock->fd, &select_wfds);
#elif defined(POLLER_POLL)
    if (mask & SOCKET_FL_READ)
        pollfds[sock->fd].events |= POLLIN;
    if (mask & SOCKET_FL_WRITE)
        pollfds[sock->fd].events |= POLLOUT;
    pollfds[sock->fd].fd = sock->fd;
#elif defined(POLLER_KQUEUE)
    if (mask & SOCKET_FL_READ) {
        EV_SET(ke, sock->fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0,
                (void *)sock);
        kev_num_changes++;
        ke = kev_change + kev_num_changes;
    }
    if (mask & SOCKET_FL_WRITE) {
        EV_SET(ke, sock->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0,
                (void *)sock);
        kev_num_changes++;
    }
#endif
}
void socket_unmonitor(isocket_t *sock, int mask) {
#ifdef POLLER_KQUEUE
    struct kevent *ke = kev_change + kev_num_changes;
#endif

    /* If this wasn't an internal call unset the want conditions so we know
     * what kind of monitoring the user no longer wants */
    if (!(mask & SOCKET_FL_INTERNAL)) {
        if (mask & SOCKET_FL_READ)
            sock->state &= ~SOCKET_FL_WANT_READ;
        if (mask & SOCKET_FL_WRITE)
            sock->state &= ~SOCKET_FL_WANT_WRITE;
    }

#if defined(POLLER_SELECT)
    if (mask & SOCKET_FL_READ)
        FD_CLR(sock->fd, &select_rfds);
    if (mask & SOCKET_FL_WRITE)
        FD_CLR(sock->fd, &select_wfds);
#elif defined(POLLER_POLL)
    if (mask & SOCKET_FL_READ)
        pollfds[sock->fd].events &= ~POLLIN;
    if (mask & SOCKET_FL_WRITE)
        pollfds[sock->fd].events &= ~POLLOUT;
    if (!pollfds[sock->fd].events)
        pollfds[sock->fd].fd = -1; /* just turn this off */
#elif defined(POLLER_KQUEUE)
    if (mask & SOCKET_FL_READ) {
        EV_SET(ke, sock->fd, EVFILT_READ, EV_DISABLE, 0, 0, (void *)sock);
        kev_num_changes++;
        ke = kev_change + kev_num_changes;
    }
    if (mask & SOCKET_FL_WRITE) {
        EV_SET(ke, sock->fd, EVFILT_WRITE, EV_DISABLE, 0, 0, (void *)sock);
        kev_num_changes++;
    }
#endif
}

/* this sets a socket as non-blocking, and possibly sets some other useful
 * options.  currently we only support fcntl() for doing this. */
static int socket_setflags(int fd) {
    int opt;

#ifdef HAVE_SETSOCKOPT
# ifdef SO_REUSEADDR
    opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&opt, sizeof(opt));
# endif
#endif

    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
        log_error("fcntl(O_NONBLOCK) failed: %s", strerror(errno));
        return 0;
    }

    return 1;
}
/* while this function may at first seem unnecessary, it is actually very
 * necessary.  we cannot safely destroy sockets in the polling loop, because
 * some event systems (namely kqueue for the present) generate two events for
 * sockets, and when one succeeds and causes a closure, there may still be an
 * event for the other which ends up getting called when it shouldn't be.
 * Also, when OpenSSL support is enabled this function performs SSL handshake
 * timeouts. */
void reap_dead_sockets(void) {
    isocket_t *sp, *sp2;

    sp = LIST_FIRST(&allsockets);

    while (sp != NULL) {
        sp2 = LIST_NEXT(sp, intlp);

        /* if it's dead, clear it away. */
        if (SOCKET_DEAD(sp)) {
            if (sp->sockaddr.addr != NULL)
                free(sp->sockaddr.addr);
            if (sp->peeraddr.addr != NULL)
                free(sp->peeraddr.addr);
            destroy_event(sp->datahook);
            LIST_REMOVE(sp, intlp);
            free(sp);
        }
#ifdef HAVE_OPENSSL
        else if (SOCKET_SSL(sp) && sp->ssl_start != 0 &&
                SOCKET_SSL_HANDSHAKING(sp) &&
                sp->ssl_start + me.ssl.hs_timeout < me.now) {
            sp->state |= SOCKET_FL_ERROR_PENDING; /* flag an error condition on
                                                     the socket so that it will
                                                     get hooked where it is
                                                     necessary. */
            sp->err = ETIMEDOUT; /* set an appropriate error condition */
        }
#endif

        sp = sp2;
    }
}

/* this function is called by the different pollers on sockets with pending
 * data.  because of SSL it is no longer as simple as activating a hook on a
 * socket. */
static inline void socket_event(isocket_t *isp) {

#ifdef HAVE_OPENSSL
    /* see if we're doing SSL on this socket.  if we are we need to make sure
     * that the SSL requisite conditions are being met for the socket.  if
     * they're not, continue to wait for them to be met. */
    if (SOCKET_SSL(isp) && !SOCKET_LISTENING(isp)) {
        if (SOCKET_SSL_HANDSHAKING(isp)) {
            /* If we're handshaking see if we can finish that off. */
            if (!socket_ssl_handshake(isp, 0)) {
                /* some kind of fatal error occured. */
                isp->state |= SOCKET_FL_ERROR_PENDING;
                return;
            }

            /* otherwise the handshake was successful.  we fall through below
             * to set/unset the read conditions as necessary and then trigger
             * the socket's hook.  even if the socket isn't monitoring at all
             * we will trip the socket hook at least once (so that the consumer
             * will be able to see that the SSL handshake has finished) */
        }
        if (isp->state & SOCKET_FL_SSLWANT_READ) {
            if (isp->state & SOCKET_FL_READ_PENDING) {
                isp->state &= ~SOCKET_FL_SSLWANT_READ;
                if (!(isp->state & SOCKET_FL_WANT_READ))
                    socket_unmonitor(isp, SOCKET_FL_READ);
            } else
                return; /* read condition not fulfilled. */
        }
        if (isp->state & SOCKET_FL_SSLWANT_WRITE) {
            if (isp->state & SOCKET_FL_WRITE_PENDING) {
                isp->state &= ~SOCKET_FL_SSLWANT_WRITE;
                if (!(isp->state & SOCKET_FL_WANT_WRITE))
                    socket_unmonitor(isp, SOCKET_FL_WRITE);
            } else
                return; /* read condition not fulfilled. */
        }
    }
#endif

    /* If we haven't done anything else, this is all we need to do. */
    hook_event(isp->datahook, isp);
    isp->state &= ~SOCKET_FL_PENDING;
}

#if defined(POLLER_SELECT)
# include "poller_select.c"
#elif defined(POLLER_POLL)
# include "poller_poll.c"
#elif defined(POLLER_KQUEUE)
# include "poller_kqueue.c"
#elif defined(POLLER_DEVPOLL)
# include "poller_devpoll.c"
#endif

#ifdef HAVE_OPENSSL
/* Below are the special SSL-only functions */
static int socket_ssl_verify_callback(int status, X509_STORE_CTX *store) {
    char buf[256];

    if (!status) {
        /* just do error reporting. */
        X509 *cert = X509_STORE_CTX_get_current_cert(store);
        int err = X509_STORE_CTX_get_error(store);

        log_warn("certificate error (depth %d): %d:%s",
                X509_STORE_CTX_get_error_depth(store), err,
                X509_verify_cert_error_string(err));
        X509_NAME_oneline(X509_get_issuer_name(cert), buf, 256);
        log_warn("issuer=%s", buf);
        X509_NAME_oneline(X509_get_subject_name(cert), buf, 256);
        log_warn("subject=%s", buf);
    }

    return status;
}

SSL_CTX *create_ssl_context(void) {
    SSL_CTX *ctx;

    if (!me.ssl.enabled)
        return NULL;

    /* Here we create our default SSL context.  This requires at least a
     * certificate file and a keyfile (probably the same file). */
    ctx = SSL_CTX_new(SSLv23_method());
    SSL_CTX_set_options(ctx, SSL_OP_ALL | SSL_OP_NO_SSLv2);
    if (*me.ssl.cafile != '\0') {
        if (SSL_CTX_load_verify_locations(ctx, me.ssl.cafile, NULL) != 1) {
            log_error("error loading SSL CA file %s: %s", me.ssl.cafile,
                    ERR_error_string(ERR_get_error(), NULL));
            SSL_CTX_free(ctx);
            return NULL;
        }
    }
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        log_error("error loading default SSL CA data: %s",
                ERR_error_string(ERR_get_error(), NULL));
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (SSL_CTX_use_certificate_chain_file(ctx, me.ssl.certfile) != 1) {
        log_error("error using certificate file in %s: %s", me.ssl.certfile,
                ERR_error_string(ERR_get_error(), NULL));
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, me.ssl.keyfile, SSL_FILETYPE_PEM) !=
            1) {
        log_error("error using private key file in %s: %s", me.ssl.keyfile,
                ERR_error_string(ERR_get_error(), NULL));
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (me.ssl.verify == true)
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, socket_ssl_verify_callback);
    else
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    return ctx;
}

/* This function enables SSL on sockets.  In the case of listening sockets it
 * simply sets the SSL flag on them as a convenience.  You are responsible for
 * checking that a socket has been accepted on an SSL listener and engaging the
 * SSL negotation using this function.  For other types of sockets this
 * function will initialize the 'ssl' member of the structure in preparation
 * for calls to socket_ssl_accept/socket_ssl_connect (socket_ssl_handshake)) */
bool socket_ssl_enable(isocket_t *isp) {

    if (!me.ssl.enabled)
        return false;

    /* Make sure the socket is in the right state. */
    if (!(isp->state & SOCKET_FL_OPEN) ||
            !((isp->state & SOCKET_FL_LISTENING) ||
                (isp->state & SOCKET_FL_OPEN)))
        return false;

    if (isp->state & SOCKET_FL_LISTENING) {
        isp->state |= SOCKET_FL_SSL;
        return true; /* that's it! */
    }

    /* otherwise... */
    if ((isp->ssl = SSL_new(me.ssl.ctx)) == NULL) {
        log_error("could not create new SSL session: %s",
                ERR_error_string(ERR_get_error(), NULL));
        return false;
    }
    if (SSL_set_fd(isp->ssl, isp->fd) != 1) {
        log_error("could not set fd on SSL: %s",
                ERR_error_string(ERR_get_error(), NULL));
        SSL_free(isp->ssl);
        isp->ssl = NULL;
        return false;
    }

    isp->state |= SOCKET_FL_SSL;
    return true;
}

/* this function initiates a client SSL connection on the given socket.  it may
 * be called multiple times if the transaction would block. */
bool socket_ssl_handshake(isocket_t *isp, bool serv) {
    int ret;

    if (!me.ssl.enabled)
        return false;

    /* If we haven't set accept/conn transaction states do so now. */
    if (!SOCKET_SSL_HANDSHAKING(isp)) {
        isp->state |= SOCKET_FL_SSL_HANDSHAKE;
        if (serv)
            SSL_set_accept_state(isp->ssl);
        else
            SSL_set_connect_state(isp->ssl);
        isp->ssl_start = me.now;
    }

    switch (SSL_get_error(isp->ssl, (ret = SSL_do_handshake(isp->ssl)))) {
        case SSL_ERROR_NONE:
            /* This is the success case which indicates that the connection has
             * been fully established. */
            isp->state &= ~SOCKET_FL_SSL_HANDSHAKE;
            return true;
        case SSL_ERROR_ZERO_RETURN:
            /* This case indicates failure at the protocl layer. */
            isp->state &= ~SOCKET_FL_SSL_HANDSHAKE;
            log_debug("while doing SSL handshake on fd %d the handshake "
                    "was shut down by the peer.", isp->fd);
            return false;
        case SSL_ERROR_WANT_READ:
            /* this is the case when we simply need more i/o later. */
            isp->state |= SOCKET_FL_SSLWANT_READ;
            socket_monitor(isp, SOCKET_FL_READ | SOCKET_FL_INTERNAL);
            return true;
        case SSL_ERROR_WANT_WRITE:
            /* this is the case when we need more i/o later.  it is slightly
             * different because we handle write-tracking differently. */
            isp->state &= ~SOCKET_FL_WRITE_PENDING;
            isp->state |= SOCKET_FL_SSLWANT_WRITE;
            socket_monitor(isp, SOCKET_FL_WRITE | SOCKET_FL_INTERNAL);
            return true;
        case SSL_ERROR_SYSCALL:
            /* this is a pretty nasty case.  basically if an OS syscall fails,
             * we get this, then we need to figure out what failed and where.
             * bleah. */
            if (ERR_peek_error() == 0) {
                if (ret == 0)
                    log_debug("while doing SSL handshake on fd %d the "
                            "handshake was improperly terminated.", isp->fd);
                else
                    log_debug("while doing SSL handshake on fd %d the "
                            "system failed: %s", isp->fd, strerror(errno));
            } else
                log_debug("while doing SSL handhskae on fd %d an error "
                        "occured: %s", isp->fd, 
                        ERR_error_string(ERR_get_error(), NULL));
            isp->state &= ~SOCKET_FL_SSL_HANDSHAKE;
            isp->state |= SOCKET_FL_ERROR_PENDING;
            isp->err = ECONNABORTED;
            return false;
        case SSL_ERROR_SSL:
            /* This is a pretty nasty case. */
            log_error("while doing SSL handshake on fd %d the SSL "
                    "library failed: %s", isp->fd,
                    ERR_error_string(ERR_get_error(), NULL));
            isp->state &= ~SOCKET_FL_SSL_HANDSHAKE;
            isp->state |= SOCKET_FL_ERROR_PENDING;
            isp->err = ECONNABORTED;
            return false;
        default:
            log_error("yikes!  unhandled case in socket_ssl_handshake!");
            isp->state |= SOCKET_FL_ERROR_PENDING;
            isp->err = 0;
            return false;
    }

    return true;
}
#endif

/* Function to determine the type of an address.  We do cheap best effort
 * here.  If it looks IPv6-y we try inet_pton with AF_INET6, if it looks
 * IPv4-y we try inet_pton with AF_INET, and as long as one succeeds we
 * return that type. */
int get_address_type(const char *addr) {
    const char *s = addr;
    unsigned char buf[IPADDR_SIZE];

#ifdef INET6
    s = strchr(addr, ':');
    if (s != NULL && strchr(s + 1, ':') != NULL) {
        /* either IPv6 or nothing, let's see */
        if (inet_pton(PF_INET6, addr, buf) == 1)
            return PF_INET6;
        return PF_UNSPEC;
    }
#endif

    if (inet_pton(PF_INET, addr, buf) == 1)
        return PF_INET;
    return PF_UNSPEC;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
