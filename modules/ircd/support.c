/*
 * support.c: handling for feature support in the server
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file provides the mechanisms to add 'ISUPPORT' features such as a list
 * of channel modes and what-have-you that are sent to clients to tell them
 * what the server supports.  It also provides a mechanism to collect
 * "handlers" for the XINFO system (extended info querying mechanism) and
 * tracking for the ATTR system (object attribute modifiers)
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: support.c 830 2009-01-25 23:08:01Z wd $");

/* make these pessimistic defaults.  they are the maximum number of tokens in
 * one line, and the maximum length of one line. */
#define ISUPPORT_MAX 12
#define ISUPPORT_MAXLEN 300

/* this function adds (or changes) an ISUPPORT value to the system.  the values
 * are sorted in alphabetical (lexocographical, really) order. */
void add_isupport(char *name, int flags, char *val) {
    struct isupport *ip = find_isupport(name);
    char buf[ISUPPORT_MAXLEN];

    /* if it already exists, wipe it out */
    if (ip != NULL)
        del_isupport(ip);

    ip = malloc(sizeof(struct isupport));
    memset(ip, 0, sizeof(struct isupport));
    strlcpy(ip->name, name, ISUPPORTNAME_MAXLEN + 1);
    /* now insert into a list.  we alphabetically sort our list, just
     * because I like to. :) */
    if (LIST_EMPTY(ircd.lists.isupport))
        LIST_INSERT_HEAD(ircd.lists.isupport, ip, lp);
    else {
        struct isupport *ip2 = LIST_FIRST(ircd.lists.isupport);
        while (ip2 != NULL) {
            if (strcasecmp(ip2->name, ip->name) > 0) {
                LIST_INSERT_BEFORE(ip2, ip, lp);
                break;
            } else if (LIST_NEXT(ip2, lp) == NULL) {
                LIST_INSERT_AFTER(ip2, ip, lp);
                break;
            }
            ip2 = LIST_NEXT(ip2, lp);
        }
    }

    if (flags == 0)
        ip->value.str = strdup(ip->name);
    else if (flags & ISUPPORT_FL_STR) {
        snprintf(buf, ISUPPORT_MAXLEN, "%s=%s", ip->name, val);
        ip->value.str = strdup(buf);
    } else if (flags & ISUPPORT_FL_INT) {
        snprintf(buf, ISUPPORT_MAXLEN, "%s=%lld", ip->name, *(int64_t *)val);
        ip->value.str = strdup(buf);
    } else if (flags & ISUPPORT_FL_PRIV)
        ip->value.priv = *(int *)val;
    ip->flags = flags;
}

/* a quick find function for ISUPPORT values */
struct isupport *find_isupport(char *name) {
    struct isupport *ip;

    LIST_FOREACH(ip, ircd.lists.isupport, lp) {
        if (!strcasecmp(ip->name, name))
            return ip;
    }

    return NULL;
}

/* and a quick delete function, as well. */
void del_isupport(struct isupport *ip) {

    if (ip != NULL) {
        LIST_REMOVE(ip, lp);
        if (!(ip->flags & ISUPPORT_FL_PRIV) && ip->value.str != NULL)
            free(ip->value.str);
        free(ip);
    }
}

/* this sends one (or more) RPL_ISUPPORTs to the connecting client.  At most it
 * will send 12 values (I may change this later to make it specifiable) */
void send_isupport(client_t *cli) {
    int tc = 0; /* token count */
    int slen = 0;
    char str[ISUPPORT_MAXLEN], ptok[ISUPPORT_MAXLEN], *token;
    struct isupport *ip;

    LIST_FOREACH(ip, ircd.lists.isupport, lp) {
        tc++;

        if (ip->flags & ISUPPORT_FL_PRIV) {
            int priv = ip->value.priv;
            /* yuck!  we have to figure out what kind of privilege this is,
             * then copy it in depending */
            if (ircd.privileges.privs[priv].flags & PRIVILEGE_FL_BOOL &&
                    BPRIV(cli, priv))
                token = ip->name;
            else if (ircd.privileges.privs[priv].flags & PRIVILEGE_FL_STR) {
                snprintf(ptok, ISUPPORT_MAXLEN, "%s=%s", ip->name,
                        SPRIV(cli, priv));
                token = ptok;
            } else {
                /* XXX: no tuple mapping yet. */
                snprintf(ptok, ISUPPORT_MAXLEN, "%s=%lld", ip->name,
                        IPRIV(cli, priv));
                token = ptok;
            }
        } else
            token = ip->value.str;

        /* check to see if we need to send the buffer.. */
        if (tc == ISUPPORT_MAX || slen + strlen(token) >= ISUPPORT_MAXLEN) {
            sendto_one(cli, RPL_FMT(cli, RPL_ISUPPORT), str);
            slen = tc = 0;
        }
        
        /* tricky code here to prevent sending an additional space at the
         * beginning of the line when one is not needed. */
        slen += sprintf(str + slen, (slen ? " %s" : "%s"), token);
    }
    /* send remnants */
    if (slen != 0)
        sendto_one(cli, RPL_FMT(cli, RPL_ISUPPORT), str);
}

/*****************************************************************************
 * XINFO support system here                                                 *
 *****************************************************************************/
/* This function will add an XINFO handler.  Handlers should be deleted when
 * they are no longer available (i.e. on module unload)  The first argument is
 * handler function, the second is the name of the handler (typically
 * uppercase) the third are any flags for the handler, the fourth is any
 * extra data that might be required (such as a privilege number), and the
 * fifth is a description of the handler (usually short).  The function returns
 * non-zero upon successful installation. */
int add_xinfo_handler(xinfo_func func, char *name, int flags, char *desc) {
    struct xinfo_handler *xhp = find_xinfo_handler(name);
    uint64_t i64 = 1;
    char privname[XINFONAME_MAXLEN + 1 + 6]; /* add 6 for "xinfo-" tag */

    if (xhp != NULL)
        return 0; /* already exists */

    xhp = malloc(sizeof(struct xinfo_handler));
    xhp->func = func;
    strlcpy(xhp->name, name, XINFONAME_MAXLEN + 1);
    xhp->flags = flags;

    /* Create a privilege for them.  Always bool, this is simply a "yes/no"
     * flag to whether or not this information is available.  Always default
     * to 1 */
    sprintf(privname, "xinfo-%s", xhp->name);
    xhp->priv = create_privilege(privname, PRIVILEGE_FL_BOOL, &i64, NULL);

    xhp->desc = strdup((desc != NULL ? desc : "no description"));

    /* insert into a sorted list */
    if (LIST_EMPTY(ircd.lists.xinfo_handlers))
        LIST_INSERT_HEAD(ircd.lists.xinfo_handlers, xhp, lp);
    else {
        struct xinfo_handler *xhp2 = LIST_FIRST(ircd.lists.xinfo_handlers);

        while (xhp2 != NULL) {
            if (strcasecmp(xhp2->name, xhp->name) > 0) {
                LIST_INSERT_BEFORE(xhp2, xhp, lp);
                break;
            } else if (LIST_NEXT(xhp2, lp) == NULL) {
                LIST_INSERT_AFTER(xhp2, xhp, lp);
                break;
            }
            xhp2 = LIST_NEXT(xhp2, lp);
        }
    }

    return 1;
}

/* A quick function to locate an XINFO handler */
struct xinfo_handler *find_xinfo_handler(char *name) {
    struct xinfo_handler *xhp;

    LIST_FOREACH(xhp, ircd.lists.xinfo_handlers, lp) {
        if (!strcasecmp(xhp->name, name))
            return xhp;
    }

    return NULL;
}

/* A simple function to remove an XINFO handler by function.  This will remove
 * any handlers with the given function, so if multiple handlers were installed
 * with with the same function they can all be removed. */
void remove_xinfo_handler(xinfo_func func) {
    struct xinfo_handler *xhp, *xhp2;

    xhp = LIST_FIRST(ircd.lists.xinfo_handlers);
    while (xhp != NULL) {
        xhp2 = LIST_NEXT(xhp, lp);

        if (xhp->func == func) {
            LIST_REMOVE(xhp, lp);
            destroy_privilege(xhp->priv);
            free(xhp->desc);
            free(xhp);
        }
        xhp = xhp2;
    }
}

/*****************************************************************************
 * XATTR support system here                                                 *
 *****************************************************************************/

#if 0
/* this function adds (or changes) an XATTR type. */
struct xattr_handler *add_xattr(xattr_func func, char *name, int flags) {
    struct xattr_handler *xhp = find_xattr(name, (flags & IRCD_ATTR_TYPES));

    if (xattr != NULL)
        free(xattr->name);
    else
        xhp = malloc(sizeof(struct xattr_handler));

    memset(xhp, 0, sizeof(struct xattr_handler));
    xhp->name = strdup(name);
    xhp->func = func;
    xhp->flags = flags;

    /* now insert into a list.  we alphabetically sort our list, just
     * because I like to. :) */
    if (LIST_EMPTY(ircd.lists.xattr))
        LIST_INSERT_HEAD(ircd.lists.xattr, xhp, lp);
    else {
        struct xattr_handler *xhp2 = LIST_FIRST(ircd.lists.xattr);
        while (xhp2 != NULL) {
            if (strcasecmp(xhp2->name, xhp->name) > 0) {
                LIST_INSERT_BEFORE(xhp2, xhp, lp);
                break;
            } else if (LIST_NEXT(xhp2, lp) == NULL) {
                LIST_INSERT_AFTER(xhp2, xhp, lp);
                break;
            }
            xhp2 = LIST_NEXT(xhp2, lp);
        }
    }

    return xhp;
}

/* find an xattr entry */
struct xattr_handler *find_xattr(char *name, uint32_t type) {
    struct xattr_handler *xhp;

    LIST_FOREACH(xhp, ircd.lists.xattr, lp) {
        if (!strcasecmp(xhp->name, name) &&
                (xhp->flags & type) == type)
            return xhp;
    }

    return NULL;
}

/* delete an xattr handler by function call */
void del_xattr(xattr_func func) {
    struct xattr_handler *xhp, *xhp2;

    xhp = LIST_FIRST(ircd.lists.xattr);
    while (xhp != NULL) {
        xhp2 = LIST_NEXT(xhp, lp);

        if (xhp->func == func) {
            LIST_REMOVE(xhp, lp);
            free(xhp->name);
            free(xhp);
        }
        xhp = xhp2;
    }
}

/* these two functions are wrappers to find and set/unset an xattr for an
 * object.  some error checking is done, and the function returns true if the
 * action was successful. */
bool set_attr(server_t *srv, char *name, uint32_t type, void *object,
        char *data) {
    struct xattr_handler *xhp = find_xattr(name, type);

    if (xhp == NULL)
        return false;

    xhp->func(xhp, srv, object, IRCD_ATTR_SET, data);
}
bool unset_attr(server_t *srv, char *name, uint32_t type, void *object) {
    struct xattr_handler *xhp = find_xattr(name, type);

    if (xhp == NULL)
        return false;

    xhp->func(xhp, srv, object, IRCD_ATTR_UNSET, NULL);
}

/* this sends a list of attributes for the object to another server.  right now
 * we respect the ISUPPORT limits set above.  the info is sent in the form:
 * ATTR NUM=1234.. BOOL WORD=foo :STRING=a string here */
void send_xattr(server_t *srv, uint32_t type, void *object) {
    int tc = 0; /* token count */
    int slen = 0;
    char str[ISUPPORT_MAXLEN], ptok[ISUPPORT_MAXLEN], *token;
    struct xattr_handler *xhp;

    LIST_FOREACH(xhp, ircd.lists.xattr, lp) {
        if ((xhp->flags & type) != type)
            continue; /* not our type */

        if (xhp->flags & IRCD_ATTR_BOOL) {
            if (xhp->func(xhp, NULL, object, IRCD_ATTR_GET, NULL) == NULL)
                continue;
            token = xhp->name;
        } else if (xhp->flags & IRCD_ATTR_INT) {
            uint64_t *ip;

            if ((ip = xhp->func(xhp, NULL, object, IRCD_ATTR_GET, NULL)) !=
                    NULL) {
                snprintf(ptok, ISUPPORT_MAXLEN, "%s=%llu", xhp->name, ip);
                token = ptok;
            }
        } else if (xhp->flags & IRCD_ATTR_WORD)
            if ((token = xhp->func(xhp, NULL, object, IRCD_ATTR_GET, NULL)) !=
                    NULL) {
                snprintf(ptok, ISUPPORT_MAXLEN, "%s=%s", xhp->name, token);
                token = ptok;


        if (ip->flags & ISUPPORT_FL_PRIV) {
            int priv = ip->value.priv;
            /* yuck!  we have to figure out what kind of privilege this is,
             * then copy it in depending */
            if (ircd.privileges.privs[priv].flags & PRIVILEGE_FL_BOOL &&
                    BPRIV(cli, priv))
                token = ip->name;
            else if (ircd.privileges.privs[priv].flags & PRIVILEGE_FL_STR) {
                snprintf(ptok, ISUPPORT_MAXLEN, "%s=%s", ip->name,
                        SPRIV(cli, priv));
                token = ptok;
            } else {
                /* XXX: no tuple mapping yet. */
                snprintf(ptok, ISUPPORT_MAXLEN, "%s=%lld", ip->name,
                        IPRIV(cli, priv));
                token = ptok;
            }
        } else
            token = ip->value.str;

        /* check to see if we need to send the buffer.. */
        if (tc == ISUPPORT_MAX || slen + strlen(token) >= ISUPPORT_MAXLEN) {
            sendto_one(cli, RPL_FMT(cli, RPL_ISUPPORT), str);
            slen = tc = 0;
        }
        slen += sprintf(str + slen, " %s", token);
    }
    /* send remnants */
    if (slen != 0)
        sendto_one(cli, RPL_FMT(cli, RPL_ISUPPORT), str);
}

#endif

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
