/*
 * socket.h: socket structures and prototypes
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: socket.h 778 2006-10-02 00:41:20Z wd $
 */

#ifndef SOCKET_H
#define SOCKET_H

/* these are some defined maximums for lengths. */
#define HOSTNAME_MAXLEN        63
#define FQDN_MAXLEN        255

#ifdef INET6
#define IPADDR_MAXLEN        39
#define IPADDR_SIZE        16
#else
#define IPADDR_MAXLEN        15
#define IPADDR_SIZE        4
#endif

typedef struct isocket isocket_t;
/* this is a subset of the data returned from getaddrinfo().  we only keep what
 * interests us (hopefully as little as possible, although...) */
struct isock_address {
    struct sockaddr *addr;  /* sockaddr * to one of a few types. */
    size_t addrlen;            /* length of addr */
    int family;                    /* PF_xxx family. */
    int type;                    /* SOCK_xxx type. */
    int protocol;            /* IPPORTO_xxx protocol, or 0 */
};
            
LIST_HEAD(isocket_list, isocket);

struct isocket {
    int            fd;                    /* file descriptor */
#ifdef HAVE_OPENSSL
    struct ssl_st *ssl;            /* SSL descriptor */
    time_t  ssl_start;            /* when handshaking started */
#endif

#define isock_laddr(x) &(x->sockaddr)
    struct isock_address sockaddr;  /* socket address */
#define isock_raddr(x) &(x->peeraddr)
    struct isock_address peeraddr;  /* address of peer */

    struct event *datahook;
    void    *udata;            /* user data...useful for when sockets are
                               hooked */

    int            err;            /* last errno on this socket. */
    uint32_t state;            /* state is set from one of the below */
#define SOCKET_FL_OPEN                0x0001
#define SOCKET_FL_LISTENING        0x0002
#define SOCKET_LISTENING(x)        ((x)->state & SOCKET_FL_LISTENING)
#define SOCKET_FL_CONNECTED        0x0004
#define SOCKET_FL_READ_PENDING        0x0008
#define SOCKET_FL_WRITE_PENDING        0x0010
#define SOCKET_FL_ERROR_PENDING        0x0020
#define SOCKET_FL_WANT_READ        0x0040
#define SOCKET_FL_WANT_WRITE        0x0080
#define SOCKET_FL_INTERNAL        0x0100
#define SOCKET_FL_EOF                0x0200

#ifdef HAVE_OPENSSL
#define SOCKET_FL_SSL                (0x0001 << 16)
#define SOCKET_SSL(x)                ((x)->state & SOCKET_FL_SSL)
#define SOCKET_FL_SSL_HANDSHAKE        (0x0002 << 16)
#define SOCKET_SSL_HANDSHAKING(x) ((x)->state & SOCKET_FL_SSL_HANDSHAKE)
#define SOCKET_FL_SSLWANT_READ        (0x0004 << 16)
#define SOCKET_FL_SSLWANT_WRITE        (0x0008 << 16)
#endif

#define SOCKET_FL_DEAD                (0x8000 << 16)

#define SOCKET_FL_READ SOCKET_FL_READ_PENDING
#define SOCKET_FL_WRITE SOCKET_FL_WRITE_PENDING
#define SOCKET_FL_PENDING (SOCKET_FL_READ_PENDING | SOCKET_FL_WRITE_PENDING | \
        SOCKET_FL_ERROR_PENDING)

#define SOCKET_READ(x)                ((x)->state & SOCKET_FL_READ_PENDING)
#define SOCKET_WRITE(x)                ((x)->state & SOCKET_FL_WRITE_PENDING)
#define SOCKET_ERROR(x)                ((x)->state & SOCKET_FL_ERROR_PENDING)
#define SOCKET_ANY(x)                ((x)->state & SOCKET_FL_PENDING)
#define SOCKET_DEAD(x)                ((x)->state & SOCKET_FL_DEAD)

    LIST_ENTRY(isocket) intlp; /* only for use in the 'allsockets' list! */
    LIST_ENTRY(isocket) lp;
};

/* init function */
void init_socketsystem(void);

/* various socket support functions */
isocket_t *create_socket(void);
int destroy_socket(isocket_t *);
int open_socket(isocket_t *);
int close_socket(isocket_t *);
int set_socket_address(struct isock_address *, char *, char *, int);
int get_socket_address(struct isock_address *, char *, size_t, int *);
int socket_listen(isocket_t *);
isocket_t *socket_accept(isocket_t *);
int socket_connect(isocket_t *, char *, char *, int);
int socket_read(isocket_t *, char *, size_t);
int socket_write(isocket_t *, char *, size_t);
const char *socket_strerror(isocket_t *);
void socket_monitor(isocket_t *, int);
void socket_unmonitor(isocket_t *, int);

/* reap dead sockets from the socket list.  make sure to only call this when
 * we're not in a polling state!  pollers won't touch dead sockets, but still.
 * be careful. */
void reap_dead_sockets(void);

/* the one external poller function, poll_sockets() will continue to handle
 * socket data input (probably forever) unless something evil happens */
int poll_sockets(time_t);

#if defined(POLLER_SELECT)
extern fd_set select_rfds, select_wfds;
#elif defined(POLLER_POLL)
extern struct pollfd *pollfds;
#elif defined(POLLER_KQUEUE)
extern int kqueuefd;
extern struct kevent *kev_list;
extern struct kevent *kev_change;
extern int kev_num_changes;
#endif

extern unsigned int cursockets, maxsockets;
extern struct isocket_list allsockets;

/* these are some ssl-specific functions which we provide for others.  in the
 * non-SSL case these are defined as macros which result in failure. */
#ifdef HAVE_OPENSSL
struct ssl_ctx_st *create_ssl_context(void);
bool socket_ssl_enable(isocket_t *);
#define socket_ssl_connect(sock) socket_ssl_handshake(sock, false)
#define socket_ssl_accept(sock) socket_ssl_handshake(sock, true)
bool socket_ssl_handshake(isocket_t *, bool);
#endif

/* Functions for handling network address type detection */
int get_address_type(const char *);

/* if we don't have a system getaddrinfo, use the ones that will be included
 * from source/contrib/gailib.c */
#ifndef HAVE_GETADDRINFO
#define addrinfo addrinfo__compat
struct addrinfo__compat;
#define getaddrinfo getaddrinfo__compat
int getaddrinfo__compat(const char *hostname, const char *servname,
        const struct addrinfo *hints, struct addrinfo **res);
#define getnameinfo getnameinfo__compat
int getnameinfo__compat(const struct sockaddr *sa, size_t salen,
        char *host, size_t hostlen, char *serv, size_t servlen, int flags);
#define gai_strerror gai_strerror__compat
const char *gai_strerror__compat(int ecode);
#define freeaddrinfo freeaddrinfo__compat
void freeaddrinfo__compat(struct addrinfo *ai);
#ifndef HAVE_INET_PTON
#undef inet_pton
#define inet_pton inet_pton__compat
const char *inet_ntop__compat(int af, const void *addr, char *numaddr,
        size_t numaddr_len);
#endif
#ifndef HAVE_INET_NTOP
#undef inet_ntop
#define inet_ntop inet_ntop__compat
int inet_pton__compat(int af, const char *hostname, void *pton);
#endif
#endif

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
