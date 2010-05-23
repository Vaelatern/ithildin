/*
 * away.h: stuff for tracking away messages.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: away.h 492 2004-01-10 16:15:06Z wd $
 */

#ifndef IRCD_COMMANDS_AWAY_H
#define IRCD_COMMANDS_AWAY_H

/* for away messages.  if the away command isn't loaded, there are no away
 * messages..  we actually put this in the core addon so that things which
 * want to show away messages can check for them without the away module
 * being loaded. */
extern struct mdext_item *away_mdext;
/* safely look for an away message. */
#define AWAYMSG(x) \
(away_mdext != NULL ? *(char **)(mdext(x, away_mdext)) : NULL)

#define RPL_AWAY 301

#endif
