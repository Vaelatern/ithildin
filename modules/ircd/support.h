/*
 * support.h: structures for various secondary feature subsystems
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: support.h 751 2006-06-23 01:43:45Z wd $
 */

#ifndef IRCD_SUPPORT_H
#define IRCD_SUPPORT_H

#define ISUPPORTNAME_MAXLEN 31 /* XXX: what does the spec say, if anything,
                                  about this...? */

/* a feature may have a straightforward string value, or it may have a value
 * which is connect-time dependent.  features which are connect-time dependent
 * are assumed to be privileges, but see below. */
struct isupport {
    char name[ISUPPORTNAME_MAXLEN + 1];
    union {
        char *str;        /* string value */
        int priv;        /* privilege value */
    } value;
#define ISUPPORT_FL_NONE    0
#define ISUPPORT_FL_STR            0x1
#define ISUPPORT_FL_INT            0x2
#define ISUPPORT_FL_PRIV    0x4
    int            flags;

    LIST_ENTRY(isupport) lp;
};

void add_isupport(char *, int, char *);
struct isupport *find_isupport(char *);
void del_isupport(struct isupport *);

void send_isupport(client_t *);

/*
 * XINFO subsystem is declared below here.  This system allows other modules to
 * provide a way of sending extended information to others via "xinfo handlers"
 */

/* xinfo handlers should take three arguments: the client requesting the
 * info (may not be local), argc, and argv.  argv starts with the name of
 * the handler, any arguments will be in argv[1]. */
typedef void (*xinfo_func)(client_t *, int, char **);
#define XINFO_FUNC(func) void func(client_t *cli, int argc __UNUSED,       \
        char **argv __UNUSED)

#define XINFONAME_MAXLEN ISUPPORTNAME_MAXLEN

/* The xinfo_handler structure contains a each xinfo handler's necessary
 * details. */
struct xinfo_handler {
    char name[XINFONAME_MAXLEN + 1];
    char *desc;
#define XINFO_HANDLER_LOCAL 0x1
#define XINFO_HANDLER_OPER  0x2
    int flags;
    int priv; /* privilege */
    xinfo_func func;

    LIST_ENTRY(xinfo_handler) lp;
};

#define XINFO_LEN 512

int add_xinfo_handler(xinfo_func, char *, int, char *);
struct xinfo_handler *find_xinfo_handler(char *);
void remove_xinfo_handler(xinfo_func);

/*
 * XATTR subsystem below here.  This system allows "extended attributes" to be
 * supported through a modular system   Attributes can be set on clients,
 * channels, or servers.
 */

#if 0
struct xattr_handler; /* XXX: fuh */
/* XATTR handlers take five arguments: the handler being used, the server making
 * the attribute change (if any), the object being acted on, the type of
 * request (set, unset, get), and the data (if any) */
#define IRCD_ATTR_UNSET -1
#define IRCD_ATTR_GET 0
#define IRCD_ATTR_SET 1
typedef void *(*xattr_func)(struct xattr_handler *, server_t *, void *, int,
        char *);
#define XATTR_HANDLER(func) void *func(struct xattr_handler *handler,        \
        server_t *srv, void *object, int request, char *data)

struct xattr_handler {
    char *name;

#define IRCD_ATTR_CLIENT        0x0001
#define IRCD_ATTR_CHANNEL        0x0002
#define IRCD_ATTR_SERVER        0x0004
#define IRCD_ATTR_TYPES                (IRCD_ATTR_CLIENT | IRCD_ATTR_CHANNEL |        \
                                 IRCD_ATTR_SERVER)

#define IRCD_ATTR_BOOL                (0x0001 << 16)
#define IRCD_ATTR_INT                (0x0002 << 16)
    /* a word differs from a string in that the 'word' form is trusted to never
     * have spaces, and can thus be sent in a place other than the end of an
     * attr line. */
#define IRCD_ATTR_STRING        (0x0004 << 16)
#define IRCD_ATTR_WORD                (0x0008 << 16)
    uint32_t flags;
    xattr_func handler;

    LIST_ENTRY(xattr_handler) lp;
};

struct xattr_handler *add_xattr(xattr_func, char *, int);
struct xattr_handler *find_xattr(char *);
void remove_xattr(xattr_func);

bool set_xattr(server_t *, char *, uint32_t, void *, char *);
bool unset_xattr(server_t *, char *, uint32_t, void *);
void send_xattr(server_t *, uint32_t, void *);
#endif

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
