/*
 * channel.c: channel structure management code
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file provides the basic functions necessary to create/destroy/manage
 * channel structures, and lists of people with channels.  It also provides the
 * basics of channel mode management.
 */

#include <ithildin/stand.h>

#include "ircd.h"

IDSTRING(rcsid, "$Id: channel.c 613 2005-11-22 13:43:19Z wd $");

channel_t *create_channel(char *name) {
    channel_t *chan = calloc(1, sizeof(channel_t));

    /* allocate space for mode data too */
    chan->mdext = mdext_alloc(ircd.mdext.channel);

    strncpy(chan->name, name, ircd.limits.chanlen);
    chan->created = me.now;

    LIST_INSERT_HEAD(ircd.lists.channels, chan, lp);
    hash_insert(ircd.hashes.channel, chan);

    ircd.stats.channels++;

    hook_event(ircd.events.channel_create, chan);

    return chan;
}

void add_to_channel(client_t *cli, channel_t *chan, bool hook) {
    struct chanlink *clp;

    /* insert user to channel list, and vice versa */
    clp = malloc(sizeof(struct chanlink));
    clp->cli = cli;
    clp->chan = chan;
    clp->flags = clp->bans = 0;
    LIST_INSERT_HEAD(&chan->users, clp, lpchan);
    LIST_INSERT_HEAD(&cli->chans, clp, lpcli);

    chan->onchannel++;

    if (hook)
        hook_event(ircd.events.channel_add, clp);
}

void del_from_channel(client_t *cli, channel_t *chan, bool hook) {
    struct chanlink *clp = clp = find_chan_link(cli, chan);

    if (clp != NULL) {
        /* hook the del call first */
        if (hook)
            hook_event(ircd.events.channel_del, clp);

        /* remove from channel/client lists */
        LIST_REMOVE(clp, lpchan);
        LIST_REMOVE(clp, lpcli);
        free(clp);
        chan->onchannel--;
    }

    if (chan->onchannel == 0)
        destroy_channel(chan);
}

void destroy_channel(channel_t *chan) {
    unsigned char *s;
    chanmode_func changefn;
    int dummy;

    hook_event(ircd.events.channel_destroy, chan);
    
    /* clear out all the data possibly left over from channel modes. */
    s = ircd.cmodes.avail;
    while (*s) {
        changefn = (chanmode_func)getsym(ircd.cmodes.modes[*s].changefunc);
        changefn(NULL, chan, *s, CHANMODE_CLEAR, NULL, &dummy);
        s++;
    }

    /* we only expect to be called on an empty channel! */
    hash_delete(ircd.hashes.channel, chan);
    LIST_REMOVE(chan, lp);
    ircd.stats.channels--;
    mdext_free(ircd.mdext.channel, chan->mdext);
    free(chan);
}

struct chanlink *find_chan_link(client_t *cli, channel_t *chan) {
    struct chanlink *clp;

    LIST_FOREACH(clp, &cli->chans, lpcli) {
        if (clp->chan == chan)
            return clp;
    }

    return NULL;
}

int channel_check_access(client_t *cli, channel_t *chan, char *ext,
        event_t *ep) {
    struct channel_check_args cca;

    cca.cli = cli;
    cca.chan = chan;
    cca.clp = find_chan_link(cli, chan);
    cca.extra = ext;

    return hook_cond_event(ep, &cca);
}

/*******************************************************************************
 * channel mode goodies here.
 ******************************************************************************/
static void chanmode_build_lists(void);

uint64_t chanmode_request(unsigned char suggested, unsigned char *actual,
        int flags, char *changefn, char *queryfn, size_t extra, void *extdata) {
    struct chanmode *md = NULL;
    int i, j;
    unsigned char c;

    /* try and find the first/best available mode */
    if (ircd.cmodes.modes[suggested].avail)
        md = &ircd.cmodes.modes[suggested];
    else {
        if (islower(suggested))
            c = toupper(suggested);
        else
            c = tolower(suggested);

        if (ircd.cmodes.modes[c].avail)
            md = &ircd.cmodes.modes[c];
        else {
            for (i = 0;i < 256;i++) {
                if (ircd.cmodes.modes[i].avail && ircd.umodes.modes[i].mask) {
                    md = &ircd.cmodes.modes[i];
                    break;
                }
            }

            if (md == NULL)
                return 0;
        }
    }

    /* fill in the stuff */
    md->avail = 0;
    md->umask = 0;
    md->prefix = '\0';
    md->changefunc = import_symbol(changefn);
    md->queryfunc = import_symbol(queryfn);
    md->flags = flags;
    md->mdi = NULL;

    if (flags == CHANMODE_FL_PREFIX) {
        /* we have to find the first free flag, if any, to use.  to do
         * this, for each value from 0x1 to 0x80 moving in powers of two,
         * we have to check if any of the channel modes are using it.  fun
         * stuff. */
        for (i = 0;i < 16;i++) {
            for (j = 0;j < 256;j++) {
                if (ircd.cmodes.modes[j].umask == 1 << i)
                    break;
            }
            if (j == 256)
                break; /* we have a weiner */
        }
        if (i == 16) {
            /* couldn't get a mode.  yuck. */
            md->avail = 1;
            return 0;
        }
        md->umask = 1 << i;
        md->prefix = *(unsigned char *)extdata;
        ircd.cmodes.pfxmap[md->prefix] = md; /* map back */
    }

    if (extra > 0)
        md->mdi = create_mdext_item(ircd.mdext.channel, extra);
    
    chanmode_build_lists();

    *actual = md->mode;
    return md->mask;
}

/* this function very simply releases a mode and calls the list rebuilder. */
void chanmode_release(unsigned char mode) {
    channel_t *chan;
    chanmode_func changefn =
        (chanmode_func)getsym(ircd.cmodes.modes[mode].changefunc);
    int dummy;
    
    /* clear the mode from all the channels .. */
    LIST_FOREACH(chan, ircd.lists.channels, lp)
        changefn(NULL, chan, mode, CHANMODE_CLEAR, NULL, &dummy);

    /* we don't touch the mode or mask members because those are set elsewhere
     * and need to be kept the way they are. */
    ircd.cmodes.modes[mode].avail = 1; /* mark it as available */

    ircd.cmodes.modes[mode].umask = 0;
    if (ircd.cmodes.modes[mode].prefix != '\0') {
        ircd.cmodes.pfxmap[ircd.cmodes.modes[mode].prefix] = NULL;
        ircd.cmodes.modes[mode].prefix = '\0';
    }
    ircd.cmodes.modes[mode].changefunc = NULL;
    ircd.cmodes.modes[mode].queryfunc = NULL;
    ircd.cmodes.modes[mode].flags = 0;
    if (ircd.cmodes.modes[mode].mdi != NULL)
        destroy_mdext_item(ircd.mdext.channel, ircd.cmodes.modes[mode].mdi);
    chanmode_build_lists();
}

static void chanmode_build_lists(void) {
    int i;
    unsigned char imodes[256];
    unsigned char *s;

    /* fill in the 'avail' modestring thing. */
    s = ircd.cmodes.avail;
    for (i = 0;i < 256;i++) {
        if (!ircd.cmodes.modes[i].avail && ircd.cmodes.modes[i].mask)
            *s++ = ircd.cmodes.modes[i].mode;
    }
    *s = '\0';

    /* now do the prefix thing.  we make two passes to make it easier. */
    s = ircd.cmodes.pmodes;
    for (i = 0;i < 256;i++) {
        if (!ircd.cmodes.modes[i].avail && ircd.cmodes.modes[i].prefix)
            *s++ = ircd.cmodes.modes[i].mode;
    }
    *s = '\0';

    s = ircd.cmodes.prefix;
    *s++ = '(';
    for (i = 0;i < 256;i++) {
        if (!ircd.cmodes.modes[i].avail && ircd.cmodes.modes[i].prefix)
            *s++ = ircd.cmodes.modes[i].mode;
    }
    *s++ = ')';
    for (i = 0;i < 256;i++) {
        if (!ircd.cmodes.modes[i].avail && ircd.cmodes.modes[i].prefix)
            *s++ = ircd.cmodes.modes[i].prefix;
    }
    *s = '\0';

    /* now do a/b/c/d/e */
    s = imodes;
#define CHANMODE_BUILD_TYPE(_flg) do {                                        \
    for (i = 0;i < 256;i++) {                                                 \
        if (!ircd.cmodes.modes[i].avail &&                                    \
                ircd.cmodes.modes[i].flags & _flg)                            \
            *s++ = ircd.cmodes.modes[i].mode;                                 \
    }                                                                         \
    *s++ = ',';                                                               \
} while (0)

    CHANMODE_BUILD_TYPE(CHANMODE_FL_A);
    CHANMODE_BUILD_TYPE(CHANMODE_FL_B);
    CHANMODE_BUILD_TYPE(CHANMODE_FL_C);
    CHANMODE_BUILD_TYPE(CHANMODE_FL_D);
    CHANMODE_BUILD_TYPE(CHANMODE_FL_E);
    *--s = '\0'; /* wipe out that last comma */
    /* also, if s - 1 is a comma (meaning we have no 'E' modes, delete that
     * comma since 'E' is non-standard */
    if (*(s - 1) == ',')
        *--s = '\0';

    /* add these two in for special mode support stuff */
    add_isupport("PREFIX", ISUPPORT_FL_STR, (char *)ircd.cmodes.prefix);
    add_isupport("CHANMODES", ISUPPORT_FL_STR, (char *)imodes);
}

int chanmode_set(unsigned char mode, client_t *cli, channel_t *chan, char *arg,
        int *argused) {
    struct chanmode *cmp = &ircd.cmodes.modes[mode];
    chanmode_func changefn;

    if (cmp->avail || !cmp->mask) /* only if it exists.. */
        return CHANMODE_NONEX;

    /* otherwise, just try and set the mode with the given functions, returning
     * the value as requested */
    changefn = (chanmode_func)getsym(cmp->changefunc);
    return changefn(cli, chan, mode, CHANMODE_SET, arg, argused);
}

int chanmode_setprefix(unsigned char prefix, channel_t *chan, char *arg,
        int *argused) {
    struct chanmode *cmp = ircd.cmodes.pfxmap[prefix];
    chanmode_func changefn;

    if (cmp == NULL)
        return CHANMODE_NONEX;

    /* found it, now set it */
    changefn = (chanmode_func)getsym(cmp->changefunc);
    return changefn(NULL, chan, cmp->mode, CHANMODE_SET, arg, argused);
}

int chanmode_unset(unsigned char mode, client_t *cli, channel_t *chan,
        char *arg, int *argused) {
    struct chanmode *cmp = &ircd.cmodes.modes[mode];
    chanmode_func changefn;

    if (cmp->avail || !cmp->mask) /* only if it exists.. */
        return CHANMODE_NONEX;

    /* otherwise, just try and set the mode with the given functions, returning
     * the value as requested */
    changefn = (chanmode_func)getsym(cmp->changefunc);
    return changefn(cli, chan, mode, CHANMODE_UNSET, arg, argused);
}

int chanmode_unsetprefix(unsigned char prefix, channel_t *chan, char *arg,
        int *argused) {
    struct chanmode *cmp = ircd.cmodes.pfxmap[prefix];
    chanmode_func changefn;

    if (cmp == NULL)
        return CHANMODE_NONEX;

    /* found it, now unset it */
    changefn = (chanmode_func)getsym(cmp->changefunc);
    return changefn(NULL, chan, cmp->mode, CHANMODE_UNSET, arg, argused);
}

int chanmode_query(unsigned char mode, channel_t *chan, char *arg,
        int *argused, void **state) {
    struct chanmode *cmp = &ircd.cmodes.modes[mode];
    chanmode_query_func queryfn;

    if (cmp->avail || !cmp->mask) /* only if it exists.. */
        return CHANMODE_NONEX;

    /* let's ask 'em about it. */
    queryfn = (chanmode_query_func)getsym(cmp->queryfunc);
    return queryfn(chan, mode, arg, argused, state);
}

int chanmode_isprefix(channel_t *chan, client_t *cli, unsigned char prefix) {
    struct chanlink *clp;
    struct chanmode *cmp = ircd.cmodes.pfxmap[prefix];

    if (cmp == NULL)
        return 0; /* no such mode. */

    /* found it, now see if it's set */
    LIST_FOREACH(clp, &chan->users, lpchan) {
        if (clp->cli == cli)
            return chanlink_ismode(clp, cmp->mode);
    }

    /* not found, no status */
    return 0;
}

char *chanmode_getprefixes(channel_t *chan, client_t *cli) {
    static unsigned char pfx[64];
    struct chanlink *clp = find_chan_link(cli, chan);
    unsigned char *s = ircd.cmodes.pmodes;
    int i = 0;

    if (clp != NULL) {
        while (*s != '\0') {
            if (clp->flags & ircd.cmodes.modes[*s].umask)
                pfx[i++] = ircd.cmodes.modes[*s].prefix;
            s++;
        }
    }

    pfx[i] = '\0';
    return (char *)pfx;
}

/* this function returns two strings.  the first is all the modes set on the
 * channel.  the second is any arguments for the modes, but may or may not be
 * truncated. */
const char **chanmode_getmodes(channel_t *chan) {
    static const char *ret[2];
    static unsigned char modes[64];
    static char *modebuf = NULL;
    static size_t mbsize = 64;
    int optused, len = 0, m = 1;
    unsigned char *i = ircd.cmodes.avail;
    void *state;

    if (modebuf == NULL)
        modebuf = malloc(mbsize);

    modes[0] = '+';
    *modebuf = '\0';

    while (*i != '\0') {
        state = NULL;

        if (chan->modes & ircd.cmodes.modes[*i].mask) {
            optused = mbsize - len;
            while (!chanmode_query(*i, chan, modebuf + len, &optused, &state)) {
                if (optused < 0) {
                    mbsize += -optused + 2;
                    modebuf = realloc(modebuf, mbsize);
                    optused = mbsize - len;
                    continue;
                }
                modes[m++] = *i;
                if (optused > 0) {
                    len += optused;
                    modebuf[len++] = ' ';
                    modebuf[len] = '\0';
                }
            }
        }
        i++;
    }
    modes[m] = '\0';
    if (len > 0)
        modebuf[--len] = '\0'; /* nerf that extra space */

    ret[0] = (char *)modes;
    ret[1] = modebuf;
    return ret;
}

char **channel_mdext_iter(char **last) {
    static int started = 0;
    channel_t *cp = *(channel_t **)last;

    if (cp == NULL) {
        if (started) {
            started = 0;
            return NULL;
        }
        cp = LIST_FIRST(ircd.lists.channels);
        started = 1;
    }
    if (cp == NULL)
        return NULL;

    *(channel_t **)last = LIST_NEXT(cp, lp);
    return (char **)&cp->mdext;
}

int chancmp(char *one, char *two, size_t len __UNUSED) {
    return istrcmp(ircd.maps.channel, one, two);
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
