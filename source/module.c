/*
 * module.c: module system support functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 */

#include <ithildin/stand.h>

IDSTRING(rcsid, "$Id: module.c 623 2005-11-27 01:09:15Z wd $");

#ifdef HAVE_DLFCN_H
# include <dlfcn.h>
#else
# ifdef HAVE_DL_H
#  include <dl.h>
# endif
#endif

/* create a module structure. */
static module_t *create_module(char *name);
static void destroy_module(module_t *);

static LIST_HEAD(, msymbol) msym_list;
static msymbol_t *find_msymbol(char *);

/* this is called both at start-up and at any time when the conf is reloaded
 * to parse the 'modules' configuration section */
void build_module_list(void) {
    conf_list_t *list = conf_find_list("modules", me.confhead, 1);
    conf_list_t *clp = NULL;
    conf_entry_t *cep = NULL, *oldcep;
    module_t *m = NULL;
    module_t *last = NULL; /* track our last addition */
    char *s = NULL;

    if (list == NULL) {
        log_warn("there is no config entity for modules, this probably "
                "won't be a very functional daemon.");
        return;
    }

    /* new configuration stuff will be read in here if it is created */

    /* use the conf_find_next bits to find what we want here */
    oldcep = cep = conf_find("module", NULL, 0, list, 1);
    /* now pull in each load statement and parse it */
    while (cep != NULL) {
        do { /* use a do/while loop so we can 'break' from it without making
                life miserable or using silly gotos */
            int new = 0;
            if (cep->type == CONF_TYPE_LIST) {
                /* check the 'string' value first, before checking the file
                 * value. */
                clp = cep->list;
                if (cep->string != NULL)
                    s = cep->string;
                else {
                    s = conf_find_entry("file", clp, 1);
                    if (s == NULL)
                        break;
                }
            } else
                s = cep->string;

            m = find_module(s);
            if (m == NULL) {
                m = create_module(s);
                new++;
            }
            if (m == NULL)
                break;

            if (cep->type == CONF_TYPE_LIST) {
                m->confdata = conf_find_list("data", clp, 1);
                if ((s = conf_find_entry("load", clp, 1)) != NULL &&
                        str_conv_bool(s, false))
                    m->flags |= MODULE_FL_AUTOLOAD;
                else
                    m->flags |= MODULE_FL_AUTOLOAD; /* default is to load. */
                if (str_conv_bool(conf_find_entry("export", clp, 1), 0))
                    m->flags |= MODULE_FL_DEPENDLOAD;
                if (cep->string != NULL) {
                    free(m->name);
                    m->name = strdup(cep->string);
                }
            } else
                m->confdata = NULL;

            if (new) {
                if (last == NULL)
                    LIST_INSERT_HEAD(&me.modules, m, lp);
                else
                    LIST_INSERT_AFTER(last, m, lp);

                last = m;
            }
        } while (0); /* just a once-through, heh */
        oldcep = cep = conf_find_next("module", NULL, 0, oldcep, list, 1);
    }

    /* now load modules which requested autoloading.  we do this here instead
     * of above so we can read in all the module data before we load
     * anything.  very handy, incidentally, modules are loaded in reverse
     * order from the file.  hopefully that won't matter to anyone :) */
    LIST_FOREACH(m, &me.modules, lp) {
        if (m->flags & MODULE_FL_AUTOLOAD && !(m->flags & MODULE_FL_LOADED))
            load_module(m->name, m->flags);
    }
}

/* this function unloads all modules rather indiscriminately.  it just starts
 * at the top of the load and starts dropping them. */
void unload_all_modules(void) {
    module_t *m;

    while ((m = LIST_FIRST(&me.modules)) != NULL) {
        unload_module(m->name);
        destroy_module(m);
    }
}

/* this function handles all the necessary work to load a module.  it first
 * checks for a .deps file in the same place as the module, and if one exists,
 * opens it and loads all dependencies listed therein.  then it opens the dll,
 * checks version headers, and calls the module's loader function. */
int load_module(char *name, int flags) {
    module_t *m = NULL;
    /* initially, we don't resolve symbols, we will run another dlopen call
     * when our dependancies are loaded with RTLD_NOW to ensure that things
     * work as they should */
    int rtld_flags = RTLD_NOW;
    char fn[33]; /* used for making function names */
    char *sn; /* short name */
    FILE *fp;
    struct module_savedata *msdp;
    struct module_dependency *mdep;
    msymbol_t *msym;

    if ((m = find_module(name)) == NULL) {
        if (!(flags & MODULE_FL_CREATE)) {
            log_warn("attempt to load module %s not in modules list.", name);
            return 0;
        } else {
            module_t *last;
            m = create_module(name);
            if (m == NULL)
                return 0;
            last = LIST_FIRST(&me.modules);
            while (last != NULL) {
                if (LIST_NEXT(last, lp) == NULL)
                    break;
                last = LIST_NEXT(last, lp);
            }

            LIST_INSERT_AFTER(last, m, lp);
        }
    }

    if (!(m->flags & MODULE_FL_LOADED) && !(flags & MODULE_FL_QUIET))
        log_notice("loading module %s...", m->name);

    /* load dependcies.  before we do, attest to the open-ness of this module.
     * this isn't, technically, true, since we haven't initialized it.
     * however, that can't be helped in the case of circular dependencies.
     * this is simply a tough area... since we don't even do the dlopen()
     * before we do the load_modules call, if you've got a circular dependency
     * that won't survive on a lazy load, you're up the creek.  sorry. */
    if (m->depfile != NULL && !(m->flags & MODULE_FL_LOADED)) {
        char sname[PATH_MAX];

        m->flags |= MODULE_FL_LOADED;
        fp = fopen(m->depfile, "r");
        if (fp != NULL) {
            while ((sn = sfgets(sname, PATH_MAX, fp)) != NULL) {
                module_t *m2 = find_module(sn);

                /* okay, depending on what's going on here, we do different
                 * things.  if the module is reloading and a dependency isn't
                 * loaded yet, we just return quietly because we assume we'll
                 * get called later (see down towards the bottom of this
                 * function), otherwise if we can't load that dependency,
                 * return an error. */
                if ((m2 == NULL || !(m2->flags & MODULE_FL_DEPENDLOAD))) {
                    if (m2 == NULL)
                        log_debug("loading dependant module %s for %s",
                                sn, name);
#if 0
                    if (m->flags & MODULE_FL_RELOADING) {
                        /* aaaaactually.  we only return if the module already
                         * exists and is *also* reloading, this way a reload
                         * with a module that has new dependencies will still
                         * work! :) */
                        if (m2 != NULL && m2->flags & MODULE_FL_RELOADING) {
                            m->flags &= ~MODULE_FL_LOADED;
                            fclose(fp);
                            return 0; /* waiting.. */
                        }
                    }
#endif
                    if (!load_module(sn, flags | MODULE_FL_DEPENDLOAD |
                            MODULE_FL_CREATE | MODULE_FL_QUIET)) {
                        log_error(
                                "loading dependant failed, giving up for %s!",
                                sn);
                        fclose(fp);
                        return 0;
                    }
                }

                /* loaded?  add our current module to this new module's
                 * dependency listing. */
                m2 = find_module(sn);
                mdep = malloc(sizeof(struct module_dependency));
                mdep->mod = m;
                LIST_INSERT_HEAD(&m2->deps, mdep, lp);

            }
            fclose(fp);
        }
        m->flags &= ~MODULE_FL_LOADED; /* undo this trickery. */
    }

    /* chceck dependencies here */
    if (flags & MODULE_FL_DEPENDLOAD) {
        if (m->flags & MODULE_FL_DEPENDLOAD && m->flags & MODULE_FL_LOADED)
            return 1; /* already depend loaded */
        rtld_flags |= RTLD_GLOBAL;
        m->flags |= MODULE_FL_DEPENDLOAD;
    }

    m->handle = dlopen(m->fullpath, rtld_flags);
    m->opencalls++;
    if (m->handle == NULL) {
        log_error("in %s: %s", m->name, dlerror());
        return 0;
    }

    if (m->flags & MODULE_FL_LOADED)
        return 1;
        
    m->header = (struct module_header *)dlsym(m->handle, "mheader");
    if (m->header == NULL) {
        log_error("in %s: %s", m->name, dlerror());
        dlclose(m->handle);
        m->opencalls = 0;
        return 0;
    }

    sn = strrchr(m->name, '/');
    if (sn == NULL)
        sn = m->name;
    else
        sn++;

    snprintf(fn, 32, "%s_loader", sn);
    m->load_function = (module_load_function)dlsym(m->handle, fn);
    snprintf(fn, 32, "%s_unloader", sn);
    m->unload_function = (module_unload_function)dlsym(m->handle, fn);

    /* check the actual mheader here for problems, warn user if it is
     * significantly older, and refuse to load if it is significantly newer.
     * (significantly newer means a major release, or two minor releases
     * ahead (i.e 2.2 -> 2.4 is a significant change!), significantly older
     * means two minor releases older, or a major release older) */
    if (m->header->baseversion.major > MAJOR_VER) {
        log_error("module %s was created for a newer %s (%i.%i(%i)), "
                "and will not be loaded.", name, BASENAME_VER,
                m->header->baseversion.major, m->header->baseversion.minor,
                m->header->baseversion.patch);
        dlclose(m->handle);
        m->opencalls = 0;
        return 0;
    } else if (m->header->baseversion.major < MAJOR_VER)
        log_warn("module %s was created for an older %s (%i.%i(%i)), "
                "and may not work", name, BASENAME_VER,
                m->header->baseversion.major, m->header->baseversion.minor,
                m->header->baseversion.patch);
    else {
        if (m->header->baseversion.minor - MINOR_VER >= 2) {
            log_error("module %s was created for a newer %s (%i.%i(%i)), "
                    "and will not be loaded.", name, BASENAME_VER,
                    m->header->baseversion.major, m->header->baseversion.minor,
                    m->header->baseversion.patch);
            dlclose(m->handle);
            m->opencalls = 0;
            return 0;
        }
        else if (m->header->baseversion.minor - MINOR_VER <= -2)
            log_warn("module %s was created for an older %s (%i.%i(%i)), "
                    "and may not work", name, BASENAME_VER,
                    m->header->baseversion.major, m->header->baseversion.minor,
                    m->header->baseversion.patch);
    } /* ugh, okay, it's an alright version <G> */

    /* go ahead and say it's loaded now, so that if it tries to load other
     * modules it will be okay. */
    m->flags |= MODULE_FL_LOADED | flags;

    /* okay, hopefully this will stay here.  I've decided to hook the
     * 'load_module' event prior to load function being called so that other
     * subsystems can do any data setup necessary that the load function might
     * wish to rely on.  hope this works. ;) */
    hook_event(me.events.load_module, m->name);

    if (m->load_function != NULL) {
        if (!m->load_function((m->flags & MODULE_FL_RELOADING ? 1 : 0),
                    &m->savedata, &m->confdata, m)) {
            /* okay, it broke...clean up and go home */
            dlclose(m->handle);
            m->opencalls = 0;
            m->flags &= ~MODULE_FL_LOADED;
            log_error("failed to load module %s", m->name);
            return 0;
        }
    }

    /* go ahead and take care of destroying the saved data for them */
    while (!SLIST_EMPTY(&m->savedata)) {
        msdp = SLIST_FIRST(&m->savedata);
        SLIST_REMOVE_HEAD(&m->savedata, lp);
        free(msdp->data);
        free(msdp->name);
        free(msdp);
    }

    /* fix symbol exports */
    LIST_FOREACH(msym, &msym_list, lp) {
        if (msym->module == m) {
            msym->val = dlsym(m->handle, msym->name);
            if (msym->val == NULL)
                log_error("yipes!  invalidated module symbol %s(%s)",
                        msym->name, m->name);
        }
    }

    /* if we are reloading, walk the dependants list and load all those, as
     * well. */
    if (m->flags & MODULE_FL_RELOADING) {
        log_debug("reloaded module %s", m->name);
        m->flags &= ~MODULE_FL_RELOADING; /* reloaded.  (keep this above) */
        LIST_FOREACH(mdep, &m->deps, lp) {
            load_module(mdep->mod->name,
                    mdep->mod->flags | MODULE_FL_DEPENDLOAD | MODULE_FL_QUIET);
        }
    }

    return 1;
}

module_t *find_module(char *name) {
    module_t *m;

    LIST_FOREACH(m, &me.modules, lp) {
        if (!strcmp(m->name, name))
            return m;
    }

    return NULL;
}

void *lookup_module_symbol(char *name, char *sym) {
    module_t *mod;
    if (*name == '\0')
        return module_symbol(NULL, sym);
    else {
        mod = find_module(name);
        if (mod != NULL)
            return module_symbol(mod, sym);
    }

    return NULL;
}

void *module_symbol(module_t *mod, const char *sym) {
    return dlsym(mod->handle, (char *) sym);
}

int unload_module(char *name) {
    module_t *m = find_module(name);
    module_t *m2;
    struct module_dependency *mdep, *mdep2;
    msymbol_t *msym;

    if (m == NULL) {
        log_warn("attempt to unloaded nonexistant module %s", name);
        return 0;
    }

    /* sure, we unloaded it!  since this may be done automatically to clean
     * up stale dependencies, and may happen more than once (possible,
     * shrug), we just silently ignore this */
    if (!(m->flags & MODULE_FL_LOADED))
        return 1; 

    /* tag the module as unloaded so we don't try and unload it again.  at this
     * point everything below ought to be successful, soooo. */
    m->flags &= ~(MODULE_FL_LOADED|MODULE_FL_DEPENDLOAD);

    /* go through their list of dependencies and unload them.  if we are
     * actually reloading, set the reload flag on all of them prior to the
     * unload call. */
    mdep = LIST_FIRST(&m->deps);
    while (mdep != NULL) {
        /* we have different behaviors depending on what we're doing.  if this
         * is a reload, we know our entries won't disappear, if it isn't, we
         * know they will! */
        if (m->flags & MODULE_FL_RELOADING) {
            mdep2 = LIST_NEXT(mdep, lp);
            if (mdep->mod->flags & MODULE_FL_LOADED) {
                mdep->mod->flags |= MODULE_FL_RELOADING;
                unload_module(mdep->mod->name);
            }
            mdep = mdep2;
        } else {
            if (mdep->mod->flags & MODULE_FL_LOADED)
                unload_module(mdep->mod->name);
            else {
                log_warn("unload_module(%s): unloaded dependency for %s",
                        m->name, mdep->mod->name);
                LIST_REMOVE(mdep, lp);
                free(mdep); /* um..? */
            }
            mdep = LIST_FIRST(&m->deps);
        }
    }

    /* similar to the way it works in load_module(), hook here, before we call
     * the unload function, so that everything is cleaned up prior to the
     * module really disappearing. */
    hook_event(me.events.unload_module, m->name);

    /* clear sailing from here, close the handle and clear out the data */
    if (m->unload_function != NULL)
        m->unload_function((m->flags & MODULE_FL_RELOADING ? 1 : 0),
                &m->savedata);
        
    while (m->opencalls) {
#ifdef DEBUG
        /* dmalloc doesn't like it when we close libraries while it's shutting
         * down.  okay, well, if me.shutdown is true, don't dlclose, but do
         * everything else.  Valgrind dislikes this too, let's just disable
         * it in the debug case. */
        if (!me.shutdown)
#endif
        if (dlclose(m->handle))
            log_error("dlclose(%s): %s", m->name, dlerror());
        m->opencalls--;
    }

    /* nullify symbols */
    LIST_FOREACH(msym, &msym_list, lp) {
        if (msym->module == m)
            msym->val = NULL;
    }

    /* now for each of our modules, if they have a dependency for this module,
     * remove it from their listing. */
    if (!(m->flags & MODULE_FL_RELOADING)) {
        LIST_FOREACH(m2, &me.modules, lp) {
            mdep = LIST_FIRST(&m2->deps);
            while (mdep != NULL) {
                mdep2 = LIST_NEXT(mdep, lp);
                if (mdep->mod == m) {
                    LIST_REMOVE(mdep, lp);
                    free(mdep);
                }
                mdep = mdep2;
            }
        }
    }

    log_debug("unloaded module %s", m->name);

    return 1;
}

int reload_module(char *name) {
    module_t *m = find_module(name);

    if (m == NULL)
        return 0;
    m->flags |= MODULE_FL_RELOADING;
    me.reloads++;

    return 1; /* the module will get reloaded later. */
}

void do_module_reloads(void) {
    module_t *m;

    LIST_FOREACH(m, &me.modules, lp) {
        if (!(m->flags & MODULE_FL_RELOADING))
            continue; /* skip */

        if (m->flags & MODULE_FL_LOADED)
            if (unload_module(m->name) == 0)
                return;

        load_module(m->name, m->flags);
    }
}

int module_loaded(char *name) {
    module_t *m = find_module(name);

    if (m != NULL && m->flags & MODULE_FL_LOADED)
        return 1;

    return 0;
}

/* this function adds data of the given size with the given name into the given
 * list.  it is useful to call this when a module is being unloaded to save its
 * data. */
void add_module_savedata(struct module_savedata_list *list, const char *name,
        size_t size, const void *data) {
    struct module_savedata *amsdp = malloc(sizeof(struct module_savedata));

    amsdp->name = strdup(name);
    amsdp->size = size;
    amsdp->data = malloc(amsdp->size);
    memcpy(amsdp->data, data, amsdp->size);
    SLIST_INSERT_HEAD(list, amsdp, lp);               
}

/* this function attempts to copy the data from a given module savedata list
 * with the given name into the given address.  If it finds the data, it copies
 * it in and returns the number of bytes copied, otherwise it returns 0. */
size_t get_module_savedata(struct module_savedata_list *list, const char *name,
        void *data) {
    struct module_savedata *gmsdp;
    SLIST_FOREACH(gmsdp, list, lp) {
        if (!strcmp(gmsdp->name, name)) {
            memcpy(data, gmsdp->data, gmsdp->size);
            return gmsdp->size;
        }
    }                       

    return 0;
}

static module_t *create_module(char *name) {
    module_t *m;
    char *s;

    m = calloc(1, sizeof(module_t));
    m->fullpath = malloc(PATH_MAX);
    m->name = strdup(name);
    /* strip off the extension */
    s = strrchr(m->name, '.');
    if (s != NULL) {
        if (!strcmp(s, ".so"))
            *s = '\0';
    }
    /* check to see if there's a dependncies file */
    sprintf(m->fullpath, "%s/%s.deps", me.lib_path, m->name);
    if (access(m->fullpath, R_OK) != -1)
        m->depfile = strdup(m->fullpath);
    else
        m->depfile = NULL;
    sprintf(m->fullpath, "%s/%s.so", me.lib_path, m->name);
    if (access(m->fullpath, R_OK) == -1) {
        log_error("unable to open module %s from %s: %s", m->name,
                m->fullpath, strerror(errno));
        if (m->depfile != NULL)
            free(m->depfile);
        free(m->name);
        free(m);
        return NULL;
    }
    return m;
}

static void destroy_module(module_t *mod) {

    LIST_REMOVE(mod, lp);
    free(mod->name);
    free(mod->fullpath);
    if (mod->depfile != NULL)
        free(mod->depfile);
    free(mod);
}

/* module symbol stuffs. */
static msymbol_t *find_msymbol(char *name) {
    msymbol_t *msp;

    LIST_FOREACH(msp, &msym_list, lp) {
        if (!strcmp(msp->name, name))
            return msp;
    }

    return NULL;
}

/* this function 'exports' a symbol (basically, creates an msymbol structure
 * for it, and finds it, etc.  it can be called by import_msymbol, and is
 * basically useful as a 'pre-declare' call. */
msymbol_t *export_symbol(char *name, module_t *mod) {
    msymbol_t *msp;
    void *obj = NULL;

    if (mod != NULL && !(mod->flags & MODULE_FL_LOADED)) {
        log_error("export_symbol(%s, %s) called with unloaded module.",
                name, mod->name);
        return NULL; /* blech. */
    }
    
    msp = find_msymbol(name);
    if (msp != NULL && msp->module != mod) {
        log_warn("attempt to overwrite msymbol %s(%s)", msp->name,
                msp->module->name);
        return msp; /* hm..? */
    }
    
    if (msp == NULL) {
        msp = malloc(sizeof(msymbol_t));
        LIST_INSERT_HEAD(&msym_list, msp, lp);
    }

    strlcpy(msp->name, name, sizeof(msp->name));
    if (mod != NULL) {
        /* they told us where to get it from.. */
        if ((obj = dlsym(mod->handle, msp->name)) == NULL) {
            log_warn("export_symbol(%s, %s): could not find symbol", msp->name,
                    mod->name);
            LIST_REMOVE(msp, lp);
            free(msp);
            return NULL;
        }
        msp->module = mod;
        msp->val = obj;
        return msp;
    } else {
        /* try the modules we know of first */
        mod = LIST_FIRST(&me.modules);
        while (obj == NULL && mod != NULL) {
            if (mod->flags & MODULE_FL_LOADED) {
                if ((obj = dlsym(mod->handle, msp->name)) != NULL)
                    break;
            }
            mod = LIST_NEXT(mod, lp);
        }
        if (obj == NULL) {
            /* no luck?  try RTLD_DEFAULT..  Actually, try NULL (the executable
             * itself), then RTLD_NEXT (all the shared modules loaded by the
             * executable .. which is actually more than we want, but will get
             * us to compile-time linked libraries when nothing else will) */
            mod = NULL;
            if ((obj = dlsym(NULL, msp->name)) == NULL)
                obj = dlsym(RTLD_NEXT, msp->name);
        }
    }
    if (obj == NULL) {
        log_warn("export_symbol(%s): could not find symbol", msp->name);
        LIST_REMOVE(msp, lp);
        free(msp);
        return NULL;
    }
    msp->module = mod;
    msp->val = obj;

    return msp;
}

/* this function is considerably simpler.  do a symbol lookup to see if we
 * already know of this symbol, if we do then pass it back to the caller,
 * otherwise try to get it from an export, then return the final value. */
msymbol_t *import_symbol(char *name) {
    msymbol_t *msp;

    msp = find_msymbol(name);
    if (msp == NULL)
        msp = export_symbol(name, NULL);

    return msp;
}

/* below here live the declarations for 'module data extensions' (mdext).  the
 * gist of this is that you create an mdext header for your structure type, and
 * then in each structure place a char pointer called 'mdext'.  now when a
 * module wants to register extra data in another module's structure, it can
 * ask for an mdext offset, with a given size, and can then use that mdext
 * offset to access into the structure.  modules are responsible for properly
 * allocating mdext space, and should provide hooks for expanding and shrinking
 * mdext data. */

#define mdext_realloc(header, data) do {                                \
    if (header->size != 0)                                                \
        data = realloc(data, header->size);                                \
    else {                                                                \
        if (data != NULL)                                                \
            free(data);                                                        \
        data = NULL;                                                        \
    }                                                                        \
} while (0)

/* this creates an mdext header.  you should have one header per structure
 * type, not per structure.  this is used as the head of the tracking data for
 * that structure. */
struct mdext_header *create_mdext_header(const char *iter) {
    struct mdext_header *mdhp = NULL;

    mdhp = calloc(1, sizeof(struct mdext_header));
    mdhp->create = create_event(EVENT_FL_NORETURN);
    mdhp->destroy = create_event(EVENT_FL_NORETURN);
    mdhp->iter = import_symbol((char *) iter);

    return mdhp;
}

/* this destroys an mdext header and all associated data. */
void destroy_mdext_header(struct mdext_header *header) {
    struct mdext_item *mip, *mip2;

    /* wipe out any remaining items. */
    mip = SLIST_FIRST(&header->items);
    while (mip != NULL) {
        mip2 = SLIST_NEXT(mip, lp);
        destroy_mdext_item(header, mip);
        mip = mip2;
    }
    destroy_event(header->create);
    destroy_event(header->destroy);
    free(header);
}

/* this creates a new mdext item, and allocates new space for it in all the
 * structures handled with the iteration function in the header. */
struct mdext_item *create_mdext_item(struct mdext_header *header,
        size_t size) {
    struct mdext_item *mip;
    char **data;
    char *rhold = NULL; /* data held for iter */
    mdext_iter_function iter = (mdext_iter_function)getsym(header->iter);

    mip = calloc(1, sizeof(struct mdext_item));
    
    mip->size = size;
    mip->offset = header->size;
    header->size += mip->size;

    /* go through and resize everything.  also, zero-fill our new section. */
    while ((data = iter(&rhold)) != NULL) {
        mdext_realloc(header, *data);
        memset(*data + mip->offset, 0, mip->size);
    }

    SLIST_INSERT_HEAD(&header->items, mip, lp);
    return mip;
}

/* this function destroys an mdext_item, and also handles resizing and
 * re-aligning the offsets in each structure and item. */
void destroy_mdext_item(struct mdext_header *header, struct mdext_item *item) {
    struct mdext_item *mip;
    struct mdext_item *old;
    char **data;
    char *rhold = NULL; /* data held for iter */
    mdext_iter_function iter = (mdext_iter_function)getsym(header->iter);

    SLIST_FOREACH(mip, &header->items, lp) {
        if (mip == item)
            break;
    }
    if (mip == NULL) {
        log_warn("tried to free an mdext_item from an mdext_header it didn't "
                "belong to!");
        return;
    }

    /* this is where it gets tricky.  we have to shift memory down, then
     * reallocate to get rid of the extra space no longer being occupied.  the
     * way we do this is by doing a memmove at the offset for the rest of the
     * data, then calling realloc.  of course, we also have to slide down the
     * offsets for everything with an offset greater than the offset of the
     * item we're deleting, too. */

    /* if we need to move (that is, if offset+size isn't equivalent to the
     * total size (making this the last item)) then do so. */
    if (mip->offset + mip->size != header->size) {
        while ((data = iter(&rhold)) != NULL) 
            memmove(*data + mip->offset, *data + (mip->offset + mip->size),
                    (header->size - mip->offset - mip->size));
    }
    header->size -= mip->size;
    rhold = NULL;
    while ((data = iter(&rhold)) != NULL)
        mdext_realloc(header, *data);

    old = mip;
    SLIST_REMOVE(&header->items, old, mdext_item, lp);
    SLIST_FOREACH(mip, &header->items, lp) {
        if (mip->offset > old->offset)
            mip->offset -= old->size; /* move down. */
    }
    free(old);
}

/* this function allocates the data for an object with the given mdext header.
 * it also calls the "create" hook, so don't call mdext_alloc more than once
 * unless you want trouble. ;) */
char *mdext_alloc(struct mdext_header *header) {
    char *data = NULL;

    if (header->size) { 
        data = calloc(1, header->size);
        hook_event(header->create, data);
    }

    return data;;
}

/* this function frees the data for ab object with the given mdext header.  it
 * also calls the "destroy" hook. */
void mdext_free(struct mdext_header *header, char *data) {

    if (data != NULL) {
        hook_event(header->destroy, data);
        free(data);
    }
}
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
