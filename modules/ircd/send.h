/*
 * send.h: support structures/prototypes for send.c
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: send.h 833 2009-05-11 01:24:59Z wd $
 */

#ifndef IRCD_SEND_H
#define IRCD_SEND_H

/*******************************************************************************
 * sendq stuff is here
 ******************************************************************************/
struct sendq_item {
    struct sendq_block *block;
    size_t offset;
    STAILQ_ENTRY(sendq_item) lp;
};

struct sendq_block {
    char    *msg;   /* message */
    int            len;    /* length of message */
    int            refs;   /* number of clients referring to this message */
};

struct sendq_block *create_sendq_block(char *, int);
void sendq_push(struct sendq_block *, connection_t *);
void sendq_pop(connection_t *);

/*******************************************************************************
 * send functions are here
 ******************************************************************************/
/* the 'send_msg' structure stores a message that is to be sent, output
 * functions for protocols should return these.  it's simply a structure
 * with a pointer to a msg, and a length field */
struct send_msg {
    char    *msg;
    int            len;
};

connection_t *cli_uplink(client_t *);
connection_t *srv_uplink(server_t *);
server_t *cli_server_uplink(client_t *);
server_t *srv_server_uplink(server_t *);

/* send a message straight to a connection (actually, queue it right away).
 * most single-senders call sendto().  if conn is NULL, don't do anything.
 * this is handy for 'pseudo-clients' which are placed in client lists for
 * convience. */
#define sendto(conn, msg, len) do {                                         \
    sendq_push(create_sendq_block(msg, len), conn);                         \
} while (0)

void sendto_one(client_t *, char *, char *, ...) __PRINTF(3);
void sendto_one_from(client_t *, client_t *, server_t *, char *, char *,
        ...) __PRINTF0(5);
void sendto_one_target(client_t *, client_t *, server_t *, char *, char *,
        char *, ...) __PRINTF0(6);
void sendto_serv(server_t *, char *, char *, ...) __PRINTF(3);
void sendto_serv_from(server_t *, client_t *, server_t *, char *, char *,
        char *, ...) __PRINTF0(6);
void sendto_serv_butone(server_t *, client_t *, server_t *, char *, char *,
        char *, ...) __PRINTF0(6);
void sendto_serv_pflag_butone(uint64_t, bool, server_t *, client_t *,
        server_t *, char *, char *, char *, ...) __PRINTF0(8);
void sendto_channel(channel_t *, client_t *, server_t *, char *, char *,
        ...) __PRINTF0(5);
void sendto_channel_local(channel_t *, client_t *, server_t *, char *, char *,
               ...) __PRINTF0(5);
void sendto_channel_remote(channel_t *, client_t *, server_t *, char *, char *,
               ...) __PRINTF0(5);
void sendto_channel_butone(channel_t *, client_t *, client_t *, server_t *,
        char *, char *, ...) __PRINTF0(6);
void sendto_channel_prefixes_butone(channel_t *, client_t *, client_t *,
        server_t *, unsigned char *, char *, char *, ...) __PRINTF0(7);
void sendto_common_channels(client_t *, server_t *, char *, char *,
        ...) __PRINTF0(4);
void sendto_match_butone(client_t *, client_t *, server_t *, char *, char *,
        char *, ...) __PRINTF(6);
void sendto_group(struct chanusers *, int, client_t *, server_t *, char *,
        char *, ...) __PRINTF(6);

/******************************************************************************
 * message flags are here
 *****************************************************************************/
struct send_flag {
    char    *name;                /* the name of the flag */
    struct chanusers users;        /* users in this flag */
    int            num;                /* the flag's number */
    int            priv;                /* the privilege for being in this flag */

/* operator only flag */
#define SEND_LEVEL_OPERATOR                0x1
/* preserve this flag even if it is operator only and the user goes -o */
#define SEND_LEVEL_PRESERVE                0x2
/* disallow manual (user) changing of this flag */
#define SEND_LEVEL_CANTCHANGE                0x4
/* a synonym for both. */
#define SEND_LEVEL_OPERATOR_PRESERVED        \
    (SEND_LEVEL_OPERATOR | SEND_LEVEL_PRESERVE)

    int            flags;                /* flags for this flag */
};
int create_send_flag(char *, int, int);
int find_send_flag(char *);
#define SFLAG(x) (find_send_flag(x))
void destroy_send_flag(int);
int add_to_send_flag(int, client_t *, bool);
struct chanlink *find_in_send_flag(int, client_t *cli);
#define in_send_flag(lev, cli) (find_in_send_flag(lev, cli) != NULL ? 1 : 0)
void remove_from_send_flag(int, client_t *, bool);
void sendto_flag(int, char *, ...) __PRINTF(2);
void sendto_flag_priv(int, int, bool, char *, ...) __PRINTF(4);
void sendto_flag_from(int, client_t *, server_t *, char *, char *,
        ...) __PRINTF(5);

/******************************************************************************
 * message/message set functions are here.
 ******************************************************************************/
/* I chose to put this stuff here instead of in another header file, since
 * the send* things rely on it heavily.  Below is the structure for
 * messages.  Messages are ONLY the actual message, so for instance a 001
 * numeric message would be "Welcome to %s %s!%s@%s".  Messages are grouped
 * into arrays, and these are arrays are stored in message sets, along with
 * a conf.  All this basically allows any number of message sets to be used.
 * It is the responsibility of individual command modules to associate the
 * proper message format with individual commands, though some are set up
 * here */

struct message_set {
    char    *name;
    char    **msgs;

    LIST_ENTRY(message_set) lp;
};

message_set_t *create_message_set(char *, conf_list_t *);
message_set_t *find_message_set(char *);
void destroy_message_set(message_set_t *);

struct message {
    char    *name;            /* the name of the message, typically associated
                               with a command */
    int     num;            /* the message's number in a message set array */
    char    *default_fmt;   /* the default format of the message */

};

#define CMSG create_message
int create_message(char *, char *);
#define DMSG destroy_message
void destroy_message(int);
int find_message(char *);

/* these define a shortcut for grabbing just the format of, or the format and
 * the name of a specific message from a client.  the macros are actually
 * client/server agnostic.  the first is good for just getting a format (for
 * non-numeric messages), the second is good for numerics. */
#define MSG_FMT(client, index)                                                \
    (client->conn != NULL ? client->conn->mset->msgs[index] :                 \
     (sptr != NULL ? sptr->conn->mset->msgs[index] :                          \
      ircd.messages.msgs[index].default_fmt))
#define RPL_FMT(client, index) ircd.messages.msgs[index].name,                \
    (client->conn != NULL ? client->conn->mset->msgs[index] :                 \
     (sptr != NULL ? sptr->conn->mset->msgs[index] :                          \
      ircd.messages.msgs[index].default_fmt))

/* below here are defines for the numerics added by default. Worth noting
 * that it is unwise to prefix numerics with leading 0s because then they
 * will be interpreted as octal numbers (not that uh.. I did that...) */
#define RPL_WELCOME 1
#define RPL_YOURHOST 2
#define RPL_CREATED 3
#define RPL_MYINFO 4
#define RPL_ISUPPORT 5
#define RPL_REDIR 10
#define RPL_LOADTOOHIGH 263

#define ERR_NOSUCHNICK 401
#define ERR_NOSUCHSERVER 402
#define ERR_NOSUCHCHANNEL 403
#define ERR_TOOMANYTARGETS 407
#define ERR_UNKNOWNCOMMAND 421
#define ERR_ERRONEOUSNICKNAME 432
#define ERR_USERNOTINCHANNEL 441
#define ERR_NOTONCHANNEL 442
#define ERR_NOTREGISTERED 451
#define ERR_NEEDMOREPARAMS 461
#define ERR_ALREADYREGISTERED 462
#define ERR_PASSWDMISMATCH 464
#define ERR_BADCHANNAME 479
#define ERR_NOPRIVILEGES 481
#define ERR_CHANBANREASON 485

#define RPL_XINFO 771

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
