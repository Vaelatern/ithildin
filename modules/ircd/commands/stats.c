/*
 * stats.c: the STATS command (wrapper for XINFO)
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: xinfo.c 496 2004-01-13 06:54:43Z wd $");

MODULE_REGISTER("$Rev: 496 $");
/*
@DEPENDENCIES@: ircd
@DEPENDENCIES@: ircd/commands/xinfo
*/

/* argv[1] = information to request
 * argv[2] ?= server to request from */
CLIENT_COMMAND(stats, 1, 2, COMMAND_FL_REGISTERED) {

    if (pass_command(cli, NULL, "STATS", "%s %s", argc, argv, 2) !=
            COMMAND_PASS_LOCAL) 
        return COMMAND_WEIGHT_HIGH;

    /* try our best to map common /stats requests to common /xinfo requests. */
    switch (*argv[1]) {
    case 'c':
    case 'C':
        strcpy(argv[1], "CONNECTS");
        argc = 2;
        break;
    case 'i':
    case 'I':
        strcpy(argv[1], "ACL");
        strcpy(argv[2], "ACCESS ALLOW STAGE 3");
        argc = 3;
        break;
    case 'k':
    case 'K':
        strcpy(argv[1], "ACL");
        strcpy(argv[2], "ACCESS DENY STAGE 3");
        argc = 3;
        break;
    case 'l':
    case 'L':
        if (argc > 2) {
            strcpy(argv[1], "CLIENT");
            argc = 3;
        } else {
            strcpy(argv[1], "ME");
            argc = 2;
        }
        break;
    case 'u':
    case 'U':
        strcpy(argv[1], "SERVER");
        argc = 2;
        break;
    case 'y':
    case 'Y':
        strcpy(argv[1], "CLASS");
        argc = 2;
        break;
    }

    strcpy(argv[0], "XINFO");
    return command_exec_client(argc, argv, cli);
}

