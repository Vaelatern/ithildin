/*
 * core.h: various useful definitions for core functionality
 * 
 * Copyright 2002-2006 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: core.h 684 2006-02-25 07:05:28Z wd $
 */

#ifndef IRCD_ADDONS_CORE_H
#define IRCD_ADDONS_CORE_H

extern struct core_addon_struct {
    struct {
        int see_hidden_chan;
    } privs;
    struct {
        unsigned char ban;
        unsigned char key;
        unsigned char limit;
        unsigned char mod;
        unsigned char nextern;
        unsigned char op;
        unsigned char secret;
        unsigned char voice;
    } chanmodes;
} core;

/* this function is handy for 'flag' type modes which don't store any extra
 * data. */
CHANMODE_FUNC(chanmode_flag);
CHANMODE_QUERY_FUNC(chanmode_flag_query);

/* this structure is used to hold bans on a channel. */
#define MAX_BANS_PER_CHANNEL 100
#define BAN_MASK_LEN NICKLEN + USERLEN + HOSTLEN + 3
LIST_HEAD(channel_ban_list, channel_ban);
struct channel_ban {
    char nick[NICKLEN + 1]; /* the three components of ban, split up for easier
                               processing. */
    char user[USERLEN + 1];
    char host[HOSTLEN + 1];
    char who[BAN_MASK_LEN + 1];
    time_t  when;       /* when the ban was set */
#define CHANNEL_BAN_BAN 0x01
    unsigned char type; /* reserved for various uses */

    LIST_ENTRY(channel_ban) lp;
};

/* some macros... the link variety are a lot faster if you've already taken
 * the time to see if they're in the channel (and have saved the chanlink
 * entry). */
#define CHANOP(cli, chan) chanmode_isprefix(chan, cli, '@')
#define CLINKOP(clp) chanlink_ismode(clp, core.chanmodes.op)
#define CHANVOICE(cli, chan) chanmode_isprefix(chan, cli, '+')
#define CLINKVOICE(clp) chanlink_ismode(clp, core.chanmodes.voice)

/* numerics goodies */
#define RPL_COMMANDSYNTAX 334
#define RPL_BANLIST 367
#define RPL_ENDOFBANLIST 368
#define ERR_CANNOTSENDTOCHAN 404
#define ERR_BANONCHAN 435
#define ERR_BANNICKCHANGE 437
#define ERR_CHANNELISFULL 471
#define ERR_BANNEDFROMCHAN 474
#define ERR_BADCHANNELKEY 475
#define ERR_BANLISTFULL 478
#define ERR_CHANOPRIVSNEEDED 482

#endif
