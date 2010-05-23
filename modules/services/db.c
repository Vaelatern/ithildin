/*
 * db.c: database wrapper
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains the routines for the services database.  Included in it
 * are wrappers for finding various things (nicknames, memos, and whatnot).
 */

#include <ithildin/stand.h>

#include "services.h"

IDSTRING(rcsid, "$Id: db.c 579 2005-08-21 06:38:18Z wd $");

static void db_read_file(char *);
static void db_write_file(char *);

int db_start(void) {
    conf_list_t *clp;

    /* set up our hash tables */
    services.db.hash.mail = create_hash_table(2, offsetof(struct regnick,
                name), HOSTLEN * 2 + 1, HASH_FL_NOCASE|HASH_FL_STRING,
            "strcasecmp");
    services.db.hash.nick = create_hash_table(128, offsetof(struct regnick,
                name), NICKLEN, HASH_FL_NOCASE|HASH_FL_STRING, "nickcmp");

    services.db.last = me.now;
    db_read_file(services.db.file);

    if ((clp = conf_find_list("administrators", *services.confhead, 1)) !=
            NULL) {
        char *s = NULL;
        regnick_t *np;

        while ((s = conf_find_entry_next("", s, clp, 1)) != NULL) {
            if ((np = db_find_nick(s)) == NULL)
                log_warn("services administrator %s is not registered!", s);
            else
                np->intflags |= NICK_IFL_ADMIN;
        }
    }

    return 1;
}

void db_cleanup(void) {

    db_sync();
}

void db_sync(void) {

    services.db.last = me.now;
    db_write_file(services.db.file);
}

/* XXX: this function assumes that the database file is IN PROPER FORMAT! */
static void db_read_file(char *file) {
    FILE *fp;
    char rbuf[16384];
    char *s, *s2;
    regnick_t *np = NULL;
    struct mail_contact *mcp;

    if ((fp = fopen(file, "r")) == NULL) {
        log_warn("could not open database file %s: %s", file,
                strerror(errno));
        return;
    }

    while (sfgets(rbuf, 16384, fp) != NULL) {
        if (!strncmp(rbuf, "nick ", 5)) {
            s = rbuf + 5; /* skip 'nick ' in the string */

            /* nickname field */
            s2 = strchr(s, ' ');
            *s2 = '\0';
            np = regnick_create(s);
            /* user field */
            s = s2 + 1;
            s2 = strchr(s, ' ');
            *s2 = '\0';
            strcpy(np->user, s);
            /* host field */
            s = s2 + 1;
            s2 = strchr(s, ' ');
            *s2 = '\0';
            strcpy(np->host, s);
            /* pass field */
            s = s2 + 1;
            s2 = strchr(s, ' ');
            *s2 = '\0';
            strcpy(np->pass, s);
            /* email field */
            s = s2 + 1;
            s2 = strchr(s, ' ');
            *s2 = '\0';
            np->email = db_find_mail(s);
            np->email->nicks++;
            /* last seen field */
            s = s2 + 1;
            np->last = strtol(s, &s2, 0);
            /* registration time field */
            s = s2 + 1;
            np->regtime = strtol(s, &s2, 0);
            /* and flags.. */
            s = s2 + 1;
            np->flags = strtoll(s, &s2, 0);
            s = s2 + 1;
            np->intflags = strtoll(s, &s2, 0);
            if (*s2 != '\0') {
                /* this is a linked in nickname.  the parent is the final
                 * field.  we gaurantee that when we write the db parent nicks
                 * are written before child nicks, so we know the lookup for
                 * the parent will be successful if the db hasn't been
                 * corrupted (hoo boy) */
                regnick_t *np2;
                s2++;
                np2 = db_find_nick(s2);
                np->parent = np2;
                LIST_INSERT_HEAD(&np2->links, np, linklp);
            }
        } else if (!strncmp(rbuf, "nick.info ", 10)) {
            s = rbuf + 10;
            strcpy(np->info, s);
        } else if (!strncmp(rbuf, "nick.access ", 12)) {
            s = rbuf + 12;
            regnick_access_add(np, s);
        } else if (!strncmp(rbuf, "mail ", 5)) {
            s = rbuf + 5;

            /* address, then last sent time.. */
            s2 = strchr(s, ' ');
            *s2++ = '\0';
            mcp = create_mail_contact(s);
            mcp->last = strtol(s2, NULL, 10);
        }
    }
}

/* Extra functions for the db write.. */
static inline void db_write_nick(FILE *, regnick_t *);

/* This function writes the database into a single file.  It actually creates a
 * file with a slightly different name, writes it in full, then performs an
 * a rename operation.  Also, the function performs expire checks while it goes
 * through the lists (why not?) */
static void db_write_file(char *file) {
    char *nfile = strdup(file);
    FILE *fp;
    struct mail_contact *mcp;
    regnick_t *np, *np2;
    int nicks, chans;
    int ex_nicks, ex_chans;
    time_t started = me.now;
    
    send_opnotice(NULL, "performing db sync/expiry...");

    /* tweak the filename ;) */
    nfile[strlen(nfile) - 1]++;
    if ((fp = fopen(nfile, "w")) == NULL) {
        log_error("could not write database to %s: %s", nfile,
                strerror(errno));
        exit_process(NULL, NULL);
    }

    nicks = ex_nicks = chans = ex_chans = 0;

    /* write mail contacts out.. */
    LIST_FOREACH(mcp, &services.db.list.mail, lp)
        fprintf(fp, "mail %s %d\n", mcp->address, mcp->last);

    /* now write nicks.. */
    np = LIST_FIRST(&services.db.list.nicks);
    while (np != NULL) {
        np2 = LIST_NEXT(np, lp);
        if (me.now - np->last >= services.expires.nick &&
                !(np->intflags & NICK_IFL_HELD)) {
            regnick_t *np_e;

            /* count the children nicks.. */
            while ((np_e = LIST_FIRST(&np->links)) != NULL)
                ex_nicks++;
            regnick_destroy(np);
            ex_nicks++;
            continue;
        }

        if (np->parent != NULL && !(np->parent->intflags & NICK_IFL_SYNCED)) {
            db_write_nick(fp, np->parent);
            nicks++;
        }
        if (!(np->intflags & NICK_IFL_SYNCED)) {
            db_write_nick(fp, np);
            nicks++;
        }

        np = np2;
    }
    LIST_FOREACH(np, &services.db.list.nicks, lp)
        np->intflags &= ~NICK_IFL_SYNCED;

    fclose(fp);
    rename(nfile, file);
    free(nfile);

    send_opnotice(NULL, "db sync finished.  %d/%d nicks written/expired, "
            "%d/%d chans written/expired, in %s", nicks, ex_nicks, chans,
            ex_chans, time_conv_str(time(NULL) - started));
}

static inline void db_write_nick(FILE *fp, regnick_t *np) {
    struct regnick_access *rap;

    fprintf(fp, "nick %s %s %s %s %s %d %d %lld %lld", np->name, np->user,
            np->host, np->pass, np->email->address, np->last, np->regtime,
            np->flags, (np->intflags & ~NICK_IFL_NOSAVE));
    if (np->parent != NULL)
        fprintf(fp, " %s", np->parent->name);
    fprintf(fp, "\n");
    fprintf(fp, "nick.info %s\n", np->info);

    SLIST_FOREACH(rap, &np->alist, lp)
        fprintf(fp, "nick.access %s@%s\n", rap->user, rap->host);
    np->intflags |= NICK_IFL_SYNCED;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
