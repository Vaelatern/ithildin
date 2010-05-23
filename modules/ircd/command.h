/*
 * command.h: command structure declarations
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: command.h 717 2006-04-25 11:39:25Z wd $
 */

#ifndef IRCD_COMMAND_H
#define IRCD_COMMAND_H

/* 32 arguments should be sufficient, hopefully? RFC1459 provides for 15, and
 * this limit is enforced elsewhere.  It might be wise to make these
 * numbers/buffer sizes tuneable. */
#define COMMAND_MAXARGS 32
#define COMMAND_MAXARGLEN 511
#define COMMAND_MAXLEN 31

/* error numbers which can be returned by the command executer and commands
 * themselves */

/* Connection has been closed.  Stop any parsing or handling of the
 * associated pointers *IMMEDIATELY* */
#define IRCD_CONNECTION_CLOSED      -1
/* Notification that the protocol has been changed.  This should cause
 * parsing to stop AFTER the finalized cleanup on the existing buffer.
 * Input functions are only gauranteed that the connection structure is
 * still valid for the buffer variables, parsing routines should never be
 * called after this is returned.*/
#define IRCD_PROTOCOL_CHANGED       -2


extern union cptr_u {
    client_t *cli;
    server_t *srv;
} cptr; /* resemble old ircd cptr, this defines the sender */
extern server_t *sptr; /* this defines the server from which this command
                          was sent TO US.  this is NOT necessarily the
                          client's server! */

#define CLIENT_COMMAND(name, minargs, maxargs, flags)                      \
    int c_ ## name ## _min = minargs;                                      \
    int c_ ## name ## _max = maxargs;                                      \
    int c_ ## name ## _flags = flags;                                      \
    int c_ ## name ## _cmd(struct command *cmd __UNUSED, int argc __UNUSED,\
            char **argv __UNUSED, client_t *cli);                          \
    int c_ ## name ## _cmd(struct command *cmd __UNUSED, int argc __UNUSED,\
            char **argv __UNUSED, client_t *cli)

#define SERVER_COMMAND(name, minargs, maxargs, flags)                      \
    int s_ ## name ## _min = minargs;                                      \
    int s_ ## name ## _max = maxargs;                                      \
    int s_ ## name ## _flags = flags;                                      \
    int s_ ## name ## _cmd(struct command *cmd __UNUSED, int argc __UNUSED,\
            char **argv __UNUSED, server_t *srv __UNUSED);                 \
    int s_ ## name ## _cmd(struct command *cmd __UNUSED, int argc __UNUSED,\
            char **argv __UNUSED, server_t *srv __UNUSED)

/* the command structure.  command modules should provide info on weighting
 * (which will probably only apply to clients), the name of the command, and
 * both a client and server version of the command.  see the modules in the
 * commands/ directory for further reference.  Individual protocols should
 * be responsible for command numbering and tokenization. */
struct command {
    int            weight;                        /* the 'weight' of this command */
#define COMMAND_WEIGHT_NONE        0
#define COMMAND_WEIGHT_LOW        2
#define COMMAND_WEIGHT_MEDIUM        5
#define COMMAND_WEIGHT_HIGH        10
#define COMMAND_WEIGHT_EXTREME        20
#define COMMAND_WEIGHT_MAX      (COMMAND_WEIGHT_EXTREME * 2)
    /* the signon grace for new clients to be somewhat exempt from flooding
     * off.  See client_exec_command in command.c */
#define COMMAND_WEIGHT_SIGNON_GRACE 30
    /* The factor by which flood count is reduced as time elapses.  For every
     * second that a client idles its flood count is reduced by
     * 1/COMMAND_WEIGHT_REDUCE_FACTOR. */
#define COMMAND_WEIGHT_REDUCE_FACTOR 8

    char    name[COMMAND_MAXLEN + 1];        /* 31-letter name is the max */
    module_t *dll;                        /* the module where this command is
                                           declared */
    conf_list_t *conf;                        /* the conf entry for this command. */
    int priv;                                /* the privilege entry for this
                                           command. */
    /* this defines the command as an 'alias'.  with an alias command, 'dll'
     * actually points to the command structure of the real command.  aliases
     * may only have a single depth. */
#define COMMAND_FL_ALIAS        0x0001
    int flags;                                /* flags for the command in general,
                                           not for one specific part */

    /* COMMAND_FL_FOLDMAX causes arguments beyond the maximum to be folded
     * into the final argument.  e.g.:  for a command FOO with maximum args
     * of 2, if the parser gathers: argv[0] == "FOO", argv[1] == "bar",
     * argv[2] == "baz", argv[3] = "luhrman", ..., FOLDMAX will change it to:
     * argv[2] == "baz luhrman ...".  Arguments will only be folded to the
     * MAXARGLEN, everything else will be lost. */
#define COMMAND_FL_FOLDMAX        0x0001
    /* these two are mutually exclusive, commands which do not specify either
     * are considered to be for both client types. (NICK, for example) */
#define COMMAND_FL_UNREGISTERED 0x0002
#define COMMAND_FL_REGISTERED        0x0004
    /* this defines a command as operator only */
#define COMMAND_FL_OPERATOR        0x0008
    /* these two are set on a command if it has an event which is being hooked
     * on.  the latter is set if an 'exclusive' hook is requested (that is, if
     * the command's function should not be executed at all.  use this with
     * care!)  These should not be set by the *_COMMAND macros or in command
     * modules at all! */
#define COMMAND_FL_HOOKED        0x0100
#define COMMAND_FL_EXCL_HOOK        0x0200
    struct {
        int        min; /* minimum and maximum arguments, and flags */
        int        max;
        int        flags;
        int        (*cmd)(struct command *, int, char **, client_t *);

        event_t        *ev;
    } client;
    struct {
        int        min; /* minimum and maximum arguments, and flags */
        int        max;
        int        flags;
        int        (*cmd)(struct command *, int, char **, server_t *);

        event_t        *ev;
    } server;

    LIST_ENTRY(command) lp;
};

/* functions to add/remove commands.  'add_command' will return 0 on
 * failure.  It will fail if it cannot load the module with the specified
 * name, or if the module doesn't have at least one of a client or server
 * command, */
int add_command(char *);
void add_command_alias(char *, char *);
void remove_command(char *);
/* find a command.  returns a NULL if no command is found.  This is NOT the
 * recommended way to call commands, instead, use the command_exec() function
 * to do this with a previously setup argc/argv */
struct command *find_command(char *);

/* this function will, based on input, send a command to a remote server.  this
 * is much akin to the 'hunt_server' function in irc2-based servers.  the
 * arguments are the client/server who are the senders, the command name, the
 * command format, the arguments (argc/argv), and the argument which contains
 * the server name (which may be a wild card).  the function returns one of
 * three values:
 * COMMAND_PASS_NONE  : no such server exists.  command unsent.  error sent.
 * COMMAND_PASS_REMOTE: server exists and is remote.  command sent.
 * COMMAND_PASS_LOCAL : server is us.
 */
#define COMMAND_PASS_NONE   -1
#define COMMAND_PASS_REMOTE 0
#define COMMAND_PASS_LOCAL  1
int pass_command(client_t *, server_t *, char *, char *, int, char **, int);

/* add a hook to a command.  this is wrapped to allow command events to be
 * created as desired, and not for all commands.  The first argument is the
 * name of the command, the second specifies whether it is for a client (1), or
 * a server (0).  the third is the function to be called, and the last is any
 * flags to be set for the command.  The function returns 0 if the command
 * doesn't exist as required, or if add_hook() fails, and 1 otherwise.
 * Additionally the 'command_hook_args' struct is defined, this structure is
 * passed along to the event to give the hook all the necessary data.  Note
 * that cli or srv will be correct depending on the caller. */
struct command_hook_args {
    int argc;
    char **argv;
    client_t *cli;
    server_t *srv;
};

int command_add_hook(char *, int, hook_function_t, int);
void command_remove_hook(char *, int, hook_function_t);

/* this is the preferred way to execute a command.  it checks for minimum
 * parameters, and does other things which are handy.  it should really be
 * the only way a command function is ever called. the value returned is
 * the value the command function returns.  the function returns 0 on
 * success, or an error (from those at the top of the file) on failure */
int command_exec_client(int, char **, client_t *);
/* the server version does things slightly different.  bogus input is still
 * checked, but instead of sending errors to the server, it sends warnings
 * etc */
int command_exec_server(int, char **, server_t *);

char *reduce_string_list(char *, char *);
#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
