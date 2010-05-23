/*
 * protocol.h: protocol structure definitions/etc
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: protocol.h 717 2006-04-25 11:39:25Z wd $
 */

#ifndef IRCD_PROTOCOL_H
#define IRCD_PROTOCOL_H

struct protocol_sender {
    client_t *client;
    server_t *server;
};

typedef struct send_msg *(*protocol_output_func)(struct protocol_sender *,
        char *, char *, char *, va_list);
struct protocol {
    char    *name;            /* name of protocol */

    /* The input function is hooked when data is available.  The return code
     * (if not 0) from the command parser should be honored and also
     * returned up the chain (it is used by the socket hook function) */
    hook_function_t input; 

    /* function called to create a new message.  the arguments are: sender
     * (should be either client or server, the protocol will know what to
     * do), command, target, format, va_list of extras */
    protocol_output_func output;
    /* function called when a new client selects this protocol.  this allows
     * protocols to select whether they expect a client/server, etc.. */
    void (*setup)(connection_t *);

    /* functions called to handle sending various types of messages to servers.
     * these will never be used to send messages to clients.  They should all
     * be filled in for servers!  Unregister hooks are not placed because the
     * QUIT/SQUIT commands seem to be ubiquitous across protocols.  Each
     * function takes as its first argument the server to send to, and then
     * takes a variable number of others as needed.  In each protocol module
     * the functions must have the names given below. */
    void (*register_user)(connection_t *, client_t *);
    void (*sync_channel)(connection_t *, channel_t *);

    /* flags specifying how the protocol behaves in certain circumstances.
     * these are somewhat hackish.  client and server protocols may have
     * different flags with the same value, so beware! */
#define PROTOCOL_SFL_SJOIN      0x0001
#define PROTOCOL_SFL_NOQUIT     0x0002
#define PROTOCOL_SFL_TSMODE     0x0004
#define PROTOCOL_SFL_ATTR       0x0008
#define PROTOCOL_SFL_TS         0x0010
#define PROTOCOL_SFL_SHORTAKILL 0x0020

    /* Top eight bits (currently) reserved for "module" flags.  Right now
     * just one, nocache, exists.  This instructs the send* functions to not
     * cache messages when they otherwise would.  This is a hack to allow
     * sending slightly different messages to different individual members
     * in a protocol (see the hostcrypt addon for a real-world example of
     * the usefulness/uselessness of this. :) */
#define PROTOCOL_MFL_NOCACHE    (0x01ULL << 56)
    uint64_t flags;

    /* the buffer size we should allocate for clients in this protocol */
    uint64_t bufsize;

    /* the module this protocol is represented by */
    module_t *dll;

    /* XXX(?):  This is a hack which I couldn't think of a better way to do.
     * Since we only want one sendq message for any protocol when sending to
     * a lot of people, when we do one of those sendto_* functions, if the
     * connection's protocol doesn't have a block ready (as in, if this is
     * NULL), create a new block and fill it in.  Right, yes, okay, now.
     * When one of those functions is done it must set tmpmsg back to NULL
     * for every protocol.  This is another one of those things I do that
     * makes the thought of threading a little hard to swallow.  I'm not
     * really sure this is worthy of that triple x above, either.  I'd
     * appreciate any comments anyone has on the matter */
    struct sendq_block *tmpmsg;
    LIST_ENTRY(protocol) lp;
};

protocol_t *find_protocol(char *);
int add_protocol(char *);
void update_protocol(char *);
void remove_protocol(char *);

HOOK_FUNCTION(protocol_default_input);

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
