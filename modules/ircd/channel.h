/*
 * channel.h: channel (mode) structures and prototypes
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: channel.h 629 2005-11-27 09:03:47Z wd $
 */

#ifndef IRCD_CHANNEL_H
#define IRCD_CHANNEL_H

/*
 * channel mode fun.  this is a lot like the usermode request/release system,
 * except that it's a bit more complicated.  There are effectively six (yes,
 * six) types of channel modes.  The first are types A-E, and work as follows:
 * Modes of type A add or remove an item from a list.  They always have
 * parameters.  Modes of type B change a setting.  They always have a
 * parameter.  Modes of type C change a setting, but only have a parameter when
 * being set.  Modes of type E (yes E) change a setting, but only have a
 * parameter when being *unset*, and modes of type D change a boolean setting
 * and never take a parameter.  Last but not least are 'PREFIX' modes which
 * change a flag on a user in the channel.  All modes should take handler
 * functions which will be called when the mode is set/unset.  The handler
 * functions should be:
 * int func(client *, channel *, unsigned char, int, char *, int *)
 * the first argument should be the client changing the mode (if any, there
 * won't always be one!) this is handy for passing errors or providing custom
 * behavior.
 * the next argument is the channel on which the mode is being set, the third
 * is the mode character given (this allows several modes to all coalesce into
 * one function if that behavior is desired), the fourth will be one of five
 * values:
 * - CHANMODE_SET to set the mode normally
 * - CHANMODE_UNSET to unset the * mode normally
 * - CHANMODE_CLEAR to clear any data held by the mode (free allocated data,
 *   etc.  called when a channel or mode is being destroyed/release)
 * the fifth argument is the next argument in the mode command. the final holds
 * a return value which specifies whether the argument was used or not.  the
 * int should be set to 1 if the argument is used, and 0 if it isn't. 
 *
 * return values:
 * CHANMODE_OK: Change/request was successfully made
 * CHANMODE_NOP: This was an n-op (nothing happened)
 * CHANMODE_FAIL: The change or request failed
 * CHANMODE_NONEX: The mode does not exist
 * CHANMODE_NOARG: The mode needed an argument and none was given
 * number > 0: Equivalent to CHANMODE_FAIL with the value of a numeric to
 *             return to the user.
 */

#define CHANMODE_FUNC(x)                                                    \
    int x(client_t *cli, channel_t *chan, unsigned char mode, int set,      \
            char *arg __UNUSED, int *argused __UNUSED)
typedef int (*chanmode_func)(client_t *, channel_t *, unsigned char,
        int, char *, int *);

/*
 * Channel mode queries:  These work a lot like the chanmode_func stuff but
 * do not expect a client.  They exist simply to return state to people who
 * want to know.  The important item is the fifth argument, a private state
 * variable.  When querying begins the pointer value pointed to be state
 * must be NULL.  The value may never be changed, and any memory allocations
 * occuring on behalf of the queryer will be freed on the final query when
 * no more data can be reset/queried.
 *
 * return values:
 * CHANMODE_OK: Change/request was successfully made
 * CHANMODE_FAIL: The change or request failed
 * CHANMODE_NONEX: The mode does not exist
 */
#define CHANMODE_QUERY_FUNC(x)                                              \
   int x(channel_t *chan, unsigned char mode, char *arg __UNUSED,           \
           int *argused __UNUSED, void **state)
typedef int (*chanmode_query_func)(channel_t *, unsigned char, char *,
        int *, void **);

#define CHANMODE_FL_A            0x01
#define CHANMODE_FL_B            0x02
#define CHANMODE_FL_C            0x04
#define CHANMODE_FL_D            0x08
#define CHANMODE_FL_E            0x10
#define CHANMODE_FL_PREFIX  0x20

#define CHANMODE_SET            1
#define CHANMODE_UNSET            0
#define CHANMODE_CLEAR            -1

#define CHANMODE_OK         0
#define CHANMODE_FAIL       -1
#define CHANMODE_NOP        -2
#define CHANMODE_NONEX      -3
#define CHANMODE_NOARG      -4

uint64_t chanmode_request(unsigned char, unsigned char *, int, char *,
        char *, size_t, void *);
void chanmode_release(unsigned char);
/* ways to set and unset channel modes.  channel modes are typically set by
 * their mode character, but can also be set by prefix in the case of userflag
 * types. */
int chanmode_set(unsigned char, client_t *, channel_t *, char *, int *);
int chanmode_setprefix(unsigned char, channel_t *, char *, int *);

int chanmode_unset(unsigned char, client_t *, channel_t *, char *, int *);
int chanmode_unsetprefix(unsigned char, channel_t *, char *, int *);

int chanmode_query(unsigned char, channel_t *, char *, int *, void **);

#define chanmode_setflag(chan, themode)                                       \
(chan->modes |= ircd.cmodes.modes[themode].mask)
#define chanmode_unsetflag(chan, themode)                                     \
(chan->modes &= ~ircd.cmodes.modes[themode].mask)

/* various checks to see if channel modes are set in specific ways.  you can
 * check to see if a flag-type mode is set on a channel, or you can check to
 * see if a user has a certain prefix.  lastly, you can get the data for a
 * specific mode (data type) */
#define chanmode_isset(chan, themode) \
((ircd.cmodes.modes[themode].avail == 0 &&                                \
  ircd.cmodes.modes[themode].mask) ?                                      \
 (chan->modes & ircd.cmodes.modes[themode].mask) :                        \
 0)

int chanmode_isprefix(channel_t *, client_t *, unsigned char);

#define chanlink_ismode(clp, mode)                                        \
(clp->flags & ircd.cmodes.modes[mode].umask)

#define chanmode_getdata(chan, themode)                                   \
((ircd.cmodes.modes[themode].avail == 0 &&                                \
  ircd.cmodes.modes[themode].mdi != NULL) ?                               \
 (chan->mdext + ircd.cmodes.modes[themode].mdi->offset) :                 \
 NULL)

#define chanmode_getintdata(chan, themode)                                \
((ircd.cmodes.modes[themode].avail == 0 &&                                \
  ircd.cmodes.modes[themode].mdi != NULL) ?                               \
 *(((uint32_t *)(chan->mdext +                                           \
             ircd.cmodes.modes[themode].mdi->offset))) :                  \
 0xFFFFFFFF)

#define chanmode_getstrdata(chan, themode)                                \
((ircd.cmodes.modes[themode].avail == 0 &&                                \
  ircd.cmodes.modes[themode].mdi != NULL) ?                               \
 ((char *)chan->mdext) + ircd.cmodes.modes[themode].mdi->offset :         \
 NULL)

/* this function returns a \0 terminated list of a client's prefixes in a
 * channel, in a static character array. */
char *chanmode_getprefixes(channel_t *, client_t *);

/* These two map prefixes to channel modes and vice versa. */
#define chanmode_prefixtomode(c)                                          \
(ircd.cmodes.pfxmap[(unsigned char)c] != NULL ?                           \
 ircd.cmodes.pfxmap[(unsigned char)c]->mode : '\0')                       
 
#define chanmode_modetoprefix(c)                                          \
(ircd.cmodes.modes[(unsigned char)c].umask ?                              \
 ircd.cmodes.modes[(unsigned char)c].prefix : '\0')

const char **chanmode_getmodes(channel_t *);

/* grab the offset of a mode. */
#define modeoffset(mode) ircd.cmodes.modes[mode].mdi->offset

struct chanmode {
    unsigned char mode;         /* the mode character */
    char    avail;              /* whether it is available or not */

    uint64_t mask;              /* the mask for this mode */
    short   umask;              /* the mask for a chanlink setting, if this is a
                                   chanuser mode */
    unsigned char prefix;       /* if this is a user-flag, it has a prefix, this is
                                   the prefix (specified by the caller in extdata).
                                   no error checking is done on this value. */
    msymbol_t *changefunc;      /* the symbol/function to change the mode */
    msymbol_t *queryfunc;       /* the symbol/function to query the mode */
    int            flags;       /* flags given for this mode */
    struct mdext_item *mdi;     /* the mdext_item which describes the channel mode.
                                   allocated automatically.  */
};

LIST_HEAD(chanusers, chanlink);
LIST_HEAD(userchans, chanlink);

/* this structure is used to glue users and channels together.  one structure
 * is allocated per user/chan relationship, and is saved in a linked list on
 * the user's side, and on the channel's side. */
struct chanlink {
    client_t  *cli;
    channel_t *chan;
    short   flags;
    short   bans; /* for users only, stores how many bans they have against
                     them */

    LIST_ENTRY(chanlink) lpcli;
    LIST_ENTRY(chanlink) lpchan;
};

char **channel_mdext_iter(char **);

struct channel {
    char    name[CHANLEN + 1];         /* our channel's name. */
    time_t  created;                   /* the timestamp as well as creation time */

    unsigned int onchannel;            /* number of people on channel */
    int            flags;
    struct chanusers users;            /* list of users in channel */
    uint64_t modes;                    /* the flag-modes for the channel. */
    char    *mdext;                    /* mdext data */

    LIST_ENTRY(channel) lp;
};

/* create a channel.  give it a name, initially the channel will be empty,
 * so you use...*/
channel_t *create_channel(char *);

/* add_to_channel.  adds the given client to the given channel, fixes up all
 * structures properly.  the third argument is set if the caller wishes the
 * channel_add event to be hooked.  this is *almost* always the case. */
void add_to_channel(client_t *, channel_t *, bool);

/* del_from_channel.  removes the user from the given channel, if the
 * channel becomes empty, it is destroyed with...  the third argument is the
 * same as above. */
void del_from_channel(client_t *, channel_t *, bool);

/* destroy_channel.  destroys a channel and returns all the good stuff to
 * memory */
void destroy_channel(channel_t *);

/* find a channel by name */
#define find_channel(name) hash_find(ircd.hashes.channel, name)

/* finds a channel/user's chanlink entity and returns it (or NULL if it doesn't
 * exist).  Also, onchannel() is a wraparound checker to give a boolean return
 * based on this info. */
struct chanlink *find_chan_link(client_t *, channel_t *);
#define onchannel(cli, chan)                                               \
    (find_chan_link(cli, chan) != NULL ? 1 : 0)

#define check_channame(name)                                               \
    (name && (*name == '#') &&                                             \
     istr_okay(ircd.maps.channel, name))

/* this structure is used by channel_check_access to pass to its various hook
 * functions when checking if a channel can have an action performed on it (or
 * something.. */
struct channel_check_args {
    channel_t *chan;            /* the channel */
    client_t *cli;            /* the client */
    struct chanlink *clp;   /* if they're in the channel, this is the link. */
    char    *extra;            /* extra data */
};

/* this function determines whether a client (cli) can perform some action
 * related to a channel (chan).  it can be used in various manners and with
 * various events.  it calls the hooks for the specified event, which should
 * return one of the five CHANNEL_ statuses above, or an error numeric to send
 * to the user (which should be of the format '%s :...' unless your function is
 * going to have some special handling). */

/* these three are synonyms for the definitions in event.h */
#define CHANNEL_CHECK_OVERRIDE        HOOK_COND_SPASS
#define CHANNEL_CHECK_OK        HOOK_COND_PASS
#define CHANNEL_CHECK_NO        HOOK_COND_FAIL
int channel_check_access(client_t *, channel_t *, char *, event_t *);

/* this lets you check to see if a user can enter a channel. */
#define can_can_join_channel(cli, chan, arg)                                \
channel_check_access(cli, chan, arg, ircd.events.can_join_channel)
/* this lets you check to see if a user can see a channel's details. */
#define can_can_see_channel(cli, chan)                                        \
channel_check_access(cli, chan, NULL, ircd.events.can_see_channel)

/* this is like the above two, except that you also pass the message the user
 * is attempting to send to the channel. */
#define can_can_send_channel(cli, chan, msg)                                \
channel_check_access(cli, chan, msg, ircd.events.can_send_channel)

/* this is like the above three, except for nick changes in the channel.
 * hopefully this is it. :) */
#define can_can_nick_channel(cli, chan, nick)                                \
channel_check_access(cli, chan, nick, ircd.events.can_nick_channel);

/* chancmp, like nickcmp in client.c */
int chancmp(char *, char *, size_t);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
