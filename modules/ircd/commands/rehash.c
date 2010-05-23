/*
 * rehash.c: the REHASH command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: rehash.c 703 2006-03-02 13:06:55Z wd $");

MODULE_REGISTER("$Rev: 703 $");
/*
@DEPENDENCIES@: ircd
*/

struct privilege_tuple priv_rehash_tuple[] = {
#define REHASH_LOCAL 0
    { "local",  REHASH_LOCAL },
#define REHASH_REMOTE 1
    { "remote", REHASH_REMOTE },
    { NULL,     0 }
};
int priv_rehash;

MODULE_LOADER(rehash) {
    int64_t i64 = 0;

    priv_rehash = create_privilege("rehash", PRIVILEGE_FL_TUPLE, &i64,
            &priv_rehash_tuple);
#define RPL_REHASHING 382
    CMSG("382", "%s :Rehashing");

    return 1;
}
MODULE_UNLOADER(rehash) {
    
    destroy_privilege(priv_rehash);
    DMSG(RPL_REHASHING);
}

/* the rehash command.  the first is an optional (and currently unused) target,
 * the second is a server to rehash on. */
CLIENT_COMMAND(rehash, 0, 2, COMMAND_FL_OPERATOR) {

    /* first check their privileges as far as remote rehashes go. */
    if (MYCLIENT(cli) && argc > 2 && find_server(argv[2]) != ircd.me &&
            TPRIV(cli, priv_rehash) != REHASH_REMOTE) {
        sendto_one(cli, RPL_FMT(cli, ERR_NOPRIVILEGES));
        return COMMAND_WEIGHT_NONE;
    }

    /* see if this rehash is for us */
    if (pass_command(cli, NULL, "REHASH", "%s %s", argc, argv, 2) !=
            COMMAND_PASS_LOCAL)
        return COMMAND_WEIGHT_NONE; /* sent along ... */

    /* it's for us. */
    sendto_one(cli, RPL_FMT(cli, RPL_REHASHING), me.conf_file);
    sendto_flag(ircd.sflag.ops, "%s is rehashing the server configuration.", cli->nick);
    log_notice("%s!%s@%s rehashed the server", cli->nick, cli->user,
            cli->host);
    reload_conf(NULL, NULL);
    return COMMAND_WEIGHT_NONE;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
