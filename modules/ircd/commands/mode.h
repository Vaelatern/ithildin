/*
 * topic.h: a container for the 'topic' structure.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: mode.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef IRCD_COMMANDS_MODE_H
#define IRCD_COMMANDS_MODE_H

/* these are the two handler functions for changing channel modes and user
 * modes. */
void user_mode(client_t *, client_t *, int, char **, int);
int channel_mode(client_t *, server_t *, channel_t *, time_t, int, char **,
        int);

#endif
