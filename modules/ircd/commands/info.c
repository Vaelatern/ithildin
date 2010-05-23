/*
 * info.c: the INFO command
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: info.c 736 2006-05-30 04:52:22Z wd $");

MODULE_REGISTER("$Rev: 736 $");
/*
@DEPENDENCIES@: ircd
*/

char **info_text_copying;
char **info_text_developers;

MODULE_LOADER(info) {
    char file[PATH_MAX];

    /* grab the data from the data directory.  We read in the COPYING file,
     * then the DEVELOPERS file. */
    sprintf(file, "%s/COPYING", me.data_path);
    info_text_copying = mmap_file_to_array(file);
    sprintf(file, "%s/DEVELOPERS", me.data_path);
    info_text_developers = mmap_file_to_array(file);

    /* now create numerics */
#define RPL_INFO 371
    CMSG("371", ":%s");
#define RPL_INFOSTART 373
    CMSG("373", ":%s Server INFO");
#define RPL_ENDOFINFO 374
    CMSG("374", ":End of /INFO list.");

    return 1;
}
MODULE_UNLOADER(info) {

    if (info_text_copying != NULL) {
        free(info_text_copying[0]);
        free(info_text_copying);
    }
    if (info_text_developers != NULL) {
        free(info_text_developers[0]);
        free(info_text_developers);
    }

    DMSG(RPL_INFO);
    DMSG(RPL_INFOSTART);
    DMSG(RPL_ENDOFINFO);
}

CLIENT_COMMAND(info, 0, 0, COMMAND_FL_REGISTERED) {
    int i, lines = 2;

    sendto_one(cli, RPL_FMT(cli, RPL_INFOSTART), ircd.me->name);
    if (info_text_copying != NULL) {
        for (i = 0;info_text_copying[i] != NULL;i++)
            sendto_one(cli, RPL_FMT(cli, RPL_INFO), info_text_copying[i]);
    }
    lines = i;
    if (info_text_developers != NULL) {
        for (i = 0;info_text_developers[i] != NULL;i++)
            sendto_one(cli, RPL_FMT(cli, RPL_INFO), info_text_developers[i]);
    }
    lines += i;
    sendto_one(cli, RPL_FMT(cli, RPL_ENDOFINFO));
    return COMMAND_WEIGHT_EXTREME;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
