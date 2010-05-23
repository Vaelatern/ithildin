/*
 * topic.h: a container for the 'topic' structure.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: topic.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef IRCD_COMMANDS_TOPIC_H
#define IRCD_COMMANDS_TOPIC_H

/* this is the stuff for topics */
extern struct mdext_item *topic_mdext;
struct channel_topic {
    char    by[NICKLEN + 1];
    time_t  set;
    char    topic[TOPICLEN + 1];
};

#define TOPIC(chan) (struct channel_topic *)(topic_mdext != NULL ?            \
        (mdext(chan, topic_mdext)) : NULL)
#endif
