/*
 * quarantine.h: functions to allow for adding/removing quarantines.
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: quarantine.h 579 2005-08-21 06:38:18Z wd $
 */

#ifndef IRCD_ADDONS_QUARANTINE_H
#define IRCD_ADDONS_QUARANTINE_H

typedef struct quarantine quarantine_t;

struct quarantine {
    char    *mask;                /* the mask which is quarantined */
    char    *class;                /* a pattern specifying the classes to which
                                   this quarantine applies, or NULL if there is
                                   no pattern. */
    char    *reason;                /* the reason, if any, for the quarantine */
    char    *type;                /* the type, if any */
    conf_list_t *conf;                /* the conf, if any, for this quarantine */

    LIST_ENTRY(quarantine) lp;
};

extern struct quarantine_module_data {
    int            bypass_priv;        /* the privilege to bypass quarantines */

    /* these two are lists of quarantined nicknames and channels */
    LIST_HEAD(, quarantine) *nicks;
    LIST_HEAD(, quarantine) *channels;
} quarantine;

quarantine_t *add_quarantine(char *);
quarantine_t *find_quarantine(char *);
void remove_quarantine(quarantine_t *);

#define ERR_ERRONEOUSNICKNAME 432

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
