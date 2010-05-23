/*
 * module.h: module structures and prototypes
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * $Id: module.h 591 2005-10-10 06:21:42Z wd $
 */

#ifndef MODULE_H
#define MODULE_H

typedef struct module module_t;

struct module_header {
    struct {
        char    major;
        char    minor;
        char    patch;
        char    pad;
    } baseversion;

    const char        *version;
};

SLIST_HEAD(module_savedata_list, module_savedata);
struct module_savedata {
    char    *name;            /* the symbol name */
    size_t  size;            /* the size of the data */
    char    *data;            /* and the actual data */

    SLIST_ENTRY(module_savedata) lp;
};

struct module_dependency {
    struct module *mod;
    LIST_ENTRY(module_dependency) lp;
};

typedef int (*module_load_function)(int, struct module_savedata_list *,
        conf_list_t **, module_t *);
typedef void (*module_unload_function)(int, struct module_savedata_list *);
struct module {
    struct module_header *header;

    char    *fullpath;            /* full path to file's module */
    char    *depfile;            /* full path to the file's dependency file, or NULL
                               if no file exists. */
    char    *name;            /* name of module */
    void    *handle;            /* dlopen() handle */
    conf_list_t *confdata;
    module_load_function load_function;
    module_unload_function unload_function;

    /* this is a list of data saved at an unload call.  it can be retrieved
     * later if/when the module is loaded again. */
    struct module_savedata_list savedata;

    LIST_HEAD(, module_dependency) deps;

    int        flags;
#define MODULE_FL_LOADED 0x1
#define MODULE_FL_DEPENDLOAD 0x2
#define MODULE_FL_AUTOLOAD 0x4

    /* the above three flags are probably not going to be of much use to
     * you, however the below three should be very handy (DO NOT USE
     * 'DEPENDLOAD' instead of 'EXPORT', the behavior of exporting vs.
     * depend loading may change in the future!).  CREATE causes the
     * module data structure to be created if it doesn't exist.  QUIET
     * suppresses all informational load messages, and EXPORT exports the
     * symbols in the module to the global process address space (beware
     * collision!) */
#define MODULE_FL_CREATE 0x0100
#define MODULE_FL_QUIET 0x0200
#define MODULE_FL_EXPORT MODULE_FL_DEPENDLOAD

    /* this one sets an internal reload state */
#define MODULE_FL_RELOADING 0x1000
    int         opencalls; /* needed to track how many times to call dlclose */

    LIST_ENTRY(module) lp;
};

/* this macro will create the 'mheader' variable for you, and creates dummy
 * 'init'/'fini' functions (remember, you're using the load/unload functions
 * and they're *NOT* init and fini!)  This should aide in debugging, and
 * makes registration very trivial.  Enjoy! */
#define MODULE_REGISTER(ver)                                               \
struct module_header mheader = {MODULE_HEADER_DATA, ver                    \
};                                                                         \
void init(void);                                                           \
void fini(void);                                                           \
void init(void) {}                                                         \
void fini(void) {}

/* these two macros declare loader/unloader functions for you.  you should use
 * them with the name of the module for which you're declaring the function.
 * the first argument in both specifies whether a 'reload' is occuring.  if a
 * reload is occuring, the module may have data it wishes to resurrect that it
 * saved in the unload call. */
#define MODULE_LOADER(x)                                                   \
int x##_loader(int __UNUSED, struct module_savedata_list * __UNUSED,       \
        conf_list_t ** __UNUSED, module_t * __UNUSED);                     \
int x##_loader(int reload __UNUSED,                                        \
        struct module_savedata_list *savelist __UNUSED,                    \
        conf_list_t **confdata __UNUSED, module_t *module __UNUSED)

#define MODULE_UNLOADER(x)                                                 \
void x##_unloader(int __UNUSED, struct module_savedata_list * __UNUSED);   \
void x##_unloader(int reload __UNUSED,                                     \
        struct module_savedata_list *savelist __UNUSED)

/* this macro should be used in the module loader function to export necessary
 * symbols */
#define EXPORT_SYM(sym) export_symbol(#sym, module)

/* and these two are wrappers on the 'savedata' functions below that take a
 * symbol and do all the necessary legwork to add/restore it */
#define MSD_ADD(sym) add_module_savedata(savelist, #sym, sizeof(sym), &sym)
#define MSD_GET(sym) get_module_savedata(savelist, #sym, &sym)

void build_module_list(void); /* should only be used in main.c */
void unload_all_modules(void); /* same */

int load_module(char *, int);
module_t *find_module(char *);
void *lookup_module_symbol(char *, char *);
void *module_symbol(module_t *, const char *);
int unload_module(char *);
int reload_module(char *);
void do_module_reloads(void);

/* returns 1 if the module exists and is loaded, 0 otherwise. */
int module_loaded(char *);

void add_module_savedata(struct module_savedata_list *, const char *, size_t,
        const void *);
size_t get_module_savedata(struct module_savedata_list *, const char *,
        void *);

/* msymbol definitions */
typedef struct msymbol msymbol_t;
struct msymbol {
    char    name[32];        /* 31 characters worth of significant name info.  if
                           this isn't enough it can be changed later.. */
    void    *val;        /* the value of the symbol */
    module_t *module;        /* the module which owns this symbol */

    LIST_ENTRY(msymbol) lp;
};

msymbol_t *export_symbol(char *, module_t *);
msymbol_t *import_symbol(char *);
#ifdef DEBUG_CODE
# define getsym(sym)                                                          \
((sym->module != NULL && sym->module->handle == NULL) ?                       \
 (log_error("referenced unavailable symbol %s from module %s", sym->name,     \
            sym->module->name)), (void *)NULL : sym->val)
#else
# define getsym(sym) (sym->val)
#endif

/* mdext stuff */
typedef char **(*mdext_iter_function)(char **);

struct mdext_item {
    int            offset; /* offset into the data */
    size_t  size;   /* size of the data at the offset */

    SLIST_ENTRY(mdext_item) lp;
};

struct mdext_header {
    size_t  size;                    /* size required for mdext */
    event_t *create;                    /* object creation event */
    event_t *destroy;                    /* and destruction event */
    SLIST_HEAD(, mdext_item) items; /* items defining this data */

    /* this function is called repeatedly to iterate over mdext entries.  It is
     * expected to return the pointer to the mdext item from the structure, or
     * NULL if there are no more structures to work on.  It is passed, for
     * convenience a void ** referring to the last item it worked on.  This
     * will be NULL at first. */
    msymbol_t *iter;
};

/* mdext management functions */
struct mdext_header *create_mdext_header(const  char *);
void destroy_mdext_header(struct mdext_header *);
struct mdext_item *create_mdext_item(struct mdext_header *, size_t);
void destroy_mdext_item(struct mdext_header *, struct mdext_item *);
char *mdext_alloc(struct mdext_header *);
void mdext_free(struct mdext_header *, char *);

/* this macro is used to access into a structure 'x' using mdext_item 'y' to
 * pull the data out. */
#define mdext(x, y) ((x)->mdext + (y)->offset)
/* this macro is like the above, but assumes 'x' is the actual data storage
 * area, not the parent object. */
#define mdext_offset(x, y) ((char *)(x) + (y)->offset)

#endif
/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
