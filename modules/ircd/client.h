/*
 * client.h: client structure declarations
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: client.h 831 2009-02-09 00:42:56Z wd $
 */

#ifndef IRCD_CLIENT_H
#define IRCD_CLIENT_H

struct client {
    char    nick[NICKLEN + 1];      /* nickname */
    char    user[USERLEN + 1];      /* username on IRC (different from username
                                       in conn */
    char    host[HOSTLEN + 1];      /* hostname on IRC */
    char    *orighost;              /* concession for host-changing modules.  this
                                       will normally point to the 'host' field of
                                       the client, but may be pointed elsewhere if
                                       need be. */
    char    ip[IPADDR_MAXLEN + 1];  /* IP address (NICKIP) */
    char    info[GCOSLEN + 1];      /* gecos/gcos info (ircname) */

#define IRCD_CLIENT_UNKNOWN         0x0001
#define IRCD_CLIENT_REGISTERED      0x0002
#define IRCD_CLIENT_KILLED          0x0004
#define IRCD_CLIENT_HISTORY         0x0008

#define CLIENT_UNKNOWN(cli) (cli->flags & IRCD_CLIENT_UNKNOWN)
#define CLIENT_REGISTERED(cli) (cli->flags & IRCD_CLIENT_REGISTERED)
#define CLIENT_KILLED(cli) (cli->flags & IRCD_CLIENT_KILLED)
#define CLIENT_HISTORY(cli) (cli->flags & IRCD_CLIENT_HISTORY)

    int     flags;

    /* check to see if this is our client.  we know it's ours if their server
     * points to us.  this was a check to see if conn was NULL, but conn can be
     * NULL for pseduo-clients. */
#define MYCLIENT(cli) (cli->server == ircd.me)
    struct connection *conn;        /* the connection for this client, NULL if
                                       remote or pseudo-client. */

    time_t  signon;                 /* signon time */
    time_t  ts;                     /* timestamp of the nickname */
    time_t  last;                   /* used for idle time (different from
                                       conn->last) */
    int     hops;                   /* how many hops away are they? */
    uint64_t modes;                 /* the user's modes, see below */

    struct userchans chans;         /* our channels */

    struct privilege_set *pset;     /* privileges (usually derived from class) */
    struct server *server;          /* server which owns this client */

    struct client_history *hist;

    char    *mdext;                 /* mdext data */
    
    LIST_ENTRY(client) lp;
};

struct client *create_client(struct connection *);
void destroy_client(struct client *, char *);
void client_change_nick(struct client *, char *);

/* find any client by name, including unregistered clients */
#define find_client_any(name) (client_t *)hash_find(ircd.hashes.client, name)
/* this is the most common case.  only find registered clients. */
client_t *find_client(char *);

/* this registers a client on the network/server.  registering a client
 * consists of checking it against stage3 acls, placing it in a connection
 * class (for real, not the default) (or dropping it if the class is full),
 * then propogating the user creation across the network, and finally
 * welcoming the user to IRC.  The function returns 1 if all this is
 * successful, 0 otherwise */
int register_client(struct client *);

/* check to see that a nickname is valid. */
int check_nickname(char *);

char *make_client_mask(char *mask);

/* mode stuff here.  modules/systems should acquire and release modes as they
 * need them.  You may not get the mode you asked for, if something else is
 * already using it, so don't assume that you did.  The function will return
 * the bitmask used to check for the mode on a user's structure, and also place
 * the actual mode letter in a character, passed as a pointer.  If you don't
 * care what mode you get specify '\0' as the suggested field.  Also, if you
 * want all users who set your specific mode to be placed in a 'send flag'
 * group (see send.[ch]) pass the value of the flag to use, if not pass -1.
 * The last argument is the name of the function used to determine whether a
 * user can set/unset the node.  The arguments are the client setting the mode,
 * the client on which the mode is being set, the mode character, an integer
 * specifying whether the mode is being set or unset, a pointer to an argument
 * (may be NULL), and a pointer to allow the function to specify whether the
 * mode used the argument or not. */
#define USERMODE_FUNC(x)                                                   \
int x(client_t *by __UNUSED, client_t *cli, unsigned char mode __UNUSED,   \
        int set __UNUSED, char *arg __UNUSED, int *argused __UNUSED)
typedef int (*usermode_func)(client_t *, client_t *, unsigned char, int,
        char *, int *);

uint64_t usermode_request(unsigned char, unsigned char *, int, int, char *);

/* use this function to release a mode if you no longer care about it being set
 * on users.  You are still responsible for clearing out the group if the mode
 * had one */
void usermode_release(unsigned char);

/* this function returns a mode string from the passed 64bit integer. the
 * string is statically allocated inside the function, if global is unset, get
 * all usermodes, otherwise, get only global modes */
unsigned char *usermode_getstr(uint64_t, char);

/* this does the opposite of the above, it returns a mask from a given string
 * of modes, if global is non-false, the mask will only contain 'global' flags,
 * otherwise, all flags are returned.  */
uint64_t usermode_getmask(unsigned char *, char);

/* this performs a 'diff' on the modes, and returns a string reflecting the
 * change from old modes to new modes, and places the result in 'result' */
void usermode_diff(uint64_t, uint64_t, char *, char);

/* this does all the work to set a mode on a user.  it checks to see if they
 * can set it, and places them in any appropriate groups.  if the function
 * succeeds it returns 1, if the user cannot set the mode it returns 0 */
int usermode_set(unsigned char, client_t *, client_t *, char *, int *);

/* this does like above, but unsetting instead of setting. */
int usermode_unset(unsigned char, client_t *, client_t *, char *, int *);

/* returns positive if a given mode is valid and set on a client */
#define usermode_isset(client, themode)                                       \
    ((ircd.umodes.modes[themode].avail == 0) ?                                \
     (client->modes & ircd.umodes.modes[themode].mask) :                      \
     0)

/* we know we will create some modes, create special macros to check them */
#define INVIS(client) (client->modes & ircd.umodes.modes['i'].mask)
#define OPER(client) (client->modes & ircd.umodes.modes['o'].mask)

struct usermode {
    unsigned char mode;             /* the actual mode */
    char    avail;                  /* 1 if available, 0 otherwise */
#define USERMODE_FL_GLOBAL  0x1     /* the usermode is spread across the
                                       network */
#define USERMODE_FL_OPER    0x2     /* the usermode is operator only */
#define USERMODE_FL_PRESERVE 0x4    /* preserve the mode once set unless
                                       explicitly unset by the user */
    int     flags;                  /* flags for the usermode */
    uint64_t mask;                  /* the bitmask for the mode */
    msymbol_t *changer;             /* the changer function for the mode */
    int     sflag;                  /* send flag (if any) for this mode. */
};

TAILQ_HEAD(client_history_list, client_history);
struct client_history {
    char    nick[NICKLEN + 1];      /* these are all the same as in the */
    char    serv[SERVLEN + 1];

    client_t *cli;                  /* points to the client we created this
                                       from.  for currently online clients the
                                       nick will be different. */
    time_t  signoff;                /* when the client signed off */

    TAILQ_ENTRY(client_history) lp;
};

struct client_history *client_add_history(client_t *);
#define client_find_history(nick)                                             \
(struct client_history *)hash_find(ircd.hashes.client_history, (nick))
client_t *client_get_history(char *, time_t);
#define client_chase client_get_history

char **client_mdext_iter(char **);

/* this structure is used by client_check_access to pass to its various hook
 * functions when checking for access */
struct client_check_args {
    client_t *from;                 /* the client performing the action */
    client_t *to;                   /* the client being acted on */
    char    *extra;                 /* any extra data */
};

#define CLIENT_CHECK_OVERRIDE   HOOK_COND_SPASS
#define CLIENT_CHECK_OK         HOOK_COND_PASS
#define CLIENT_CHECK_NO         HOOK_COND_FAIL
int client_check_access(client_t *, client_t *, char *, event_t *);
#define can_can_send_client(from, to, arg) \
client_check_access(cli, to, arg, ircd.events.can_send_client)
#define can_can_nick_client(cli, arg) \
client_check_access(cli, NULL, arg, ircd.events.can_nick_client)

/* this is used by the hash function to compare nicks, use our resident
 * table */
int nickcmp(char *, char *, size_t);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
