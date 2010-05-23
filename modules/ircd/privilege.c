/*
 * privilege.c: various functions used for handling privileges
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file provides the systems for creating privileges and privile sets, it
 * is very similar to the last third of send.c, but has a few enhancements.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: privilege.c 579 2005-08-21 06:38:18Z wd $");

char *privilege_find_setting(int num, conf_list_t *clp);
void set_privilege_in_set(int num, privilege_set_t *psp, conf_list_t *clp);

/* this is defined to support set_privilege_in_set.  it recurses through the
 * given conf list, trying to find an 'include' item first, then attempting to
 * find the conf in the current list pointer.  yuck. */
char *privilege_find_setting(int num, conf_list_t *clp) {
    char *ent = NULL;
    char *ret = NULL;
    conf_entry_t *cep;

    ent = conf_find_entry(ircd.privileges.privs[num].name, clp, 1);
    if (ent != NULL)
        return ent; /* we need look no further. */

    ent = NULL;
    while ((ent = conf_find_entry_next("include", ent, clp, 1)) != NULL) {
        cep = conf_find("privilege-set", ent, CONF_TYPE_LIST, *ircd.confhead,
            1);
        if (cep != NULL) {
            ret = privilege_find_setting(num, cep->list);
            if (ret != NULL)
                return ret;
        }
    }

    return NULL;
}

/* this function acts slightly different than its counterpart in send.c.  In
 * this instance, we simply take the conf that is the head of the privilege
 * set, and iterate along the "include" directives, setting the value as we
 * find it, from the lowest depth on up.  local values override remote ones.
 * if we can't manage to find *any* setting, then and only then do we use the
 * default.  yeesh. */
void set_privilege_in_set(int num, privilege_set_t *psp, conf_list_t *clp) {
    char *s;
    int i;

    s = privilege_find_setting(num, clp);
    if (s == NULL) { /* didn't find anything? */
        if (ircd.privileges.privs[num].flags & PRIVILEGE_FL_STR)
            psp->vals[num] = strdup(ircd.privileges.privs[num].default_val);
        else {
            psp->vals[num] = malloc(sizeof(int64_t));
            *(int64_t *)psp->vals[num] =
                *(int64_t *)ircd.privileges.privs[num].default_val;
        }
    } else {
        /* error check the value if we got one. */
        if (ircd.privileges.privs[num].flags & PRIVILEGE_FL_STR) {
            psp->vals[num] = strdup(s);
            return;
        }

        /* otherwise, it's integral. */
        psp->vals[num] = malloc(sizeof(int64_t));
        if (ircd.privileges.privs[num].flags & PRIVILEGE_FL_INT) {
            *(int64_t *)psp->vals[num] = str_conv_int(s,
                    *(int64_t *)ircd.privileges.privs[num].default_val);
            return;
        } else if (ircd.privileges.privs[num].flags & PRIVILEGE_FL_BOOL) {
            *(int64_t *)psp->vals[num] = (str_conv_bool(s,
                    *(int64_t *)ircd.privileges.privs[num].default_val) ?
                    1 : 0);
            return;
        }
        /* handle tuples.  we walk through the tuple structure passed,
         * looking for a pair we can use.  if no pair is found, we use the
         * default value. */
        *(int64_t *)psp->vals[num] =
            *(int64_t *)ircd.privileges.privs[num].default_val;
        for (i = 0;ircd.privileges.privs[num].tuples[i].name != NULL;i++) {
            if (!strcasecmp(ircd.privileges.privs[num].tuples[i].name, s)) {
                *(int64_t *)psp->vals[num] =
                    ircd.privileges.privs[num].tuples[i].val;
                break;
            }
        }
        if (ircd.privileges.privs[num].tuples[i].name == NULL) {
            /* warn them about their bogus entry. */
            log_warn("setting %s is not valid for privilege %s", s,
                    ircd.privileges.privs[num].name);
        }
    }
}

/* this function creates a privilege set. */
privilege_set_t *create_privilege_set(char *name, conf_list_t *conf) {
    privilege_set_t *psp;
    int i;

    psp = find_privilege_set(name);
    if (psp != NULL) {
        log_debug("updating privilege set %s", name);
        for (i = 0;i < ircd.privileges.count;i++) {
            if (ircd.privileges.privs[i].num < 0)
                continue;
            free(psp->vals[i]);
        }
    } else {
        psp = malloc(sizeof(privilege_set_t));
        psp->name = strdup(name);
        psp->vals = malloc(sizeof(char *) * ircd.privileges.size);

        /* insert into the list. */
        if (LIST_FIRST(ircd.privileges.sets) == NULL)
            LIST_INSERT_HEAD(ircd.privileges.sets, psp, lp);
        else
            LIST_INSERT_AFTER(LIST_FIRST(ircd.privileges.sets), psp, lp);
    }

    for (i = 0;i < ircd.privileges.count;i++) {
        if (ircd.privileges.privs[i].num < 0)
            continue;
        set_privilege_in_set(i, psp, conf);
    }

    return psp;
}

privilege_set_t *find_privilege_set(char *name) {
    privilege_set_t *psp;

    LIST_FOREACH(psp, ircd.privileges.sets, lp) {
        if (!strcasecmp(psp->name, name))
            return psp;
    }
    return NULL;
}
                
/* this is, currently, a dangerous function to call.  we don't know what-all
 * refers to our current set, and as such we should probably not just do
 * this.  */
void destroy_privilege_set(privilege_set_t *set) {
    int i;

    free(set->name);
    for (i = 0;i < ircd.privileges.count;i++) {
        if (set->vals[i] != NULL)
            free(set->vals[i]);
    }

    LIST_REMOVE(set, lp);
    free(set);
}

/* this function creates a new privilege type with the given name and given
 * flags/default value.  Also, you may wish to pass extra data (currently only
 * a tuple listing).  it will return a number by which the privilege can be
 * indexed by array (or using the special macros provided below).  It also
 * updates existing privilege sets to reflect the new privilege. */
int create_privilege(char *name, int flags, void *val, void *extra) {
    privilege_t pp;
    privilege_set_t *psp;
    int i = 0;

    if (val == NULL || name == NULL || *name == '\0')
        return -1;

    pp.num = find_privilege(name);
    if (pp.num != -1) {
        log_warn("tried to create a privilege which already exists. (%s)",
                name);
        return -1;
    }

    pp.name = strdup(name);
    pp.flags = flags;
    if (flags & PRIVILEGE_FL_STR)
        pp.default_val = strdup(val);
    else {
        pp.default_val = malloc(sizeof(int64_t));
        if (flags & PRIVILEGE_FL_BOOL)
            *(int64_t *)pp.default_val = (*(int64_t *)val ? 1 : 0);
        else
            *(int64_t *)pp.default_val = *(int64_t *)val;
        if (flags & PRIVILEGE_FL_TUPLE)
            pp.tuples = extra;
    }

    if (ircd.privileges.count == ircd.privileges.size) {
        /* try finding an empty space in the current array. */
        for (i = 0;i < ircd.privileges.count;i++) {
            if (ircd.privileges.privs[i].num < 0) {
                pp.num = i; /* an empty one, grab it. */
                break;
            }
        }
        if (i == ircd.privileges.count) {
            ircd.privileges.size += 512; /* allocate 512 more slots */
            ircd.privileges.privs = realloc(ircd.privileges.privs,
                    sizeof(privilege_t) * ircd.privileges.size);
            /* zero out the fresh memory */
            memset(ircd.privileges.privs + (sizeof(privilege_t) *
                        (ircd.privileges.size - 512)), 0,
                    sizeof(privilege_t) * 512);

            /* re-allocate the arrays for our sets, too */
            LIST_FOREACH(psp, ircd.privileges.sets, lp) {
                psp->vals = realloc(psp->vals, sizeof(void *) *
                        ircd.privileges.size);
                memset(psp->vals + (sizeof(void *) *
                            (ircd.privileges.size - 512)), 0,
                        sizeof(void *) * 512);
            }
            pp.num = ircd.privileges.count++;
        }
    } else
        pp.num = ircd.privileges.count++; /* use the next number, and increment
                                             count */

    memcpy(&ircd.privileges.privs[pp.num], &pp, sizeof(privilege_t));

    /* we've now added the new privilege type, but we also need to set the
     * configured or default value in each privilege set.  This is made extra
     * difficult by the stacking nature of privilege sets.  We just use an
     * external function to do this.  */
    LIST_FOREACH(psp, ircd.privileges.sets, lp) {
        conf_entry_t *cep = conf_find("privilege-set", psp->name,
                CONF_TYPE_LIST, *ircd.confhead, 1);

        if (cep != NULL)
            set_privilege_in_set(pp.num, psp, cep->list);
    }

    return pp.num;
}

/* this function completely obliterates a privilege from working.  it should
 * only be called if you are 100% sure you'll no longer ever use the privilege
 * (suitable for unload calls) */
void destroy_privilege(int num) {
    struct privilege_set *psp;

    if (num >= ircd.privileges.count)
        return; /* uhh.. */

    free(ircd.privileges.privs[num].name);
    ircd.privileges.privs[num].name = NULL; /* this indicates a dead entry. */
    ircd.privileges.privs[num].num = -1; /* this too. :) */
    ircd.privileges.privs[num].flags = 0;
    free(ircd.privileges.privs[num].default_val);
    ircd.privileges.privs[num].default_val = NULL;
    ircd.privileges.privs[num].tuples = NULL;

    /* also, free all the values in all the sets */
    LIST_FOREACH(psp, ircd.privileges.sets, lp) {
        free(psp->vals[num]);
        psp->vals[num] = NULL;
    }
}

/* find a privilege with the given name.  this isn't a particularly fast
 * operation, and as such should *NOT* be used often (really probably only
 * in create/destroy_message...but) */
int find_privilege(char *name) {
    int i;

    for (i = 0; i < ircd.privileges.count;i++) {
        if (ircd.privileges.privs[i].name != NULL &&
                !strcasecmp(ircd.privileges.privs[i].name, name))
            return i;
    }
        
    return -1;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
