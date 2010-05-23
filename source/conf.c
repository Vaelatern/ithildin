/*
 * conf.c: configuration file parser
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This file contains the routines responsible for extracting data from
 * configuration files.  The file format is documented lightly in doc/conf.txt.
 * This really ought to be written in lex/yacc.
 */

#include <ithildin/stand.h>

IDSTRING(rcsid, "$Id: conf.c 831 2009-02-09 00:42:56Z wd $");

/* these are parsing functions */
static char *conf_preparse(char *, char *);
static conf_list_t *conf_parse(char *, int, char *);
static conf_entry_t *merge_into_conf(conf_list_t *, conf_entry_t *,
        conf_list_t *);
static char *conf_expand_text(char *);

#define parse_err(x) log_error("%s:%d %s", file, line, x);

/* this function reads the configuration data from 'file' and turns it into a
 * tree headed at the conf_list structure it returns.  The rest of the
 * functions in this file can then be used to look at this data.  */
conf_list_t *read_conf(char *file) {
    char *stuff = NULL;
    conf_list_t *list = NULL;

    /* mmap the data in */
    if ((stuff = mmap_file(file)) == NULL)
        return NULL;

    /* weed out comments, and bad strings */
    if ((stuff = conf_preparse(file, stuff)) == NULL)
        return NULL;
    
    /* now parse */
    list = conf_parse(file, 1, stuff);
    /* 'stuff' is now not necessary one way or the other */
    free(stuff);

    if (list == NULL)
        return NULL;

    /* display_tree(0, list);*/
    return list;
}

/* this is used to free the memory consumed in a conf_list.  it works
 * recursively, clearing all data down the line. */
void destroy_conf_branch(conf_list_t *list) {
    conf_entry_t *ep1, *ep2 = NULL;

    ep1 = LIST_FIRST(list);
    while (ep1 != NULL) {
        ep2 = LIST_NEXT(ep1, lp);
        if (ep1->type == CONF_TYPE_LIST)
            destroy_conf_branch(ep1->list);
        if (ep1->string != NULL)
            free(ep1->string);
        free(ep1->name);
        free(ep1);
        ep1 = ep2;
    }
}

/* this is a debugging function whicb prints the data in 'list' out in a
 * tree-like format to stdout. */
void conf_display_tree(int depth, conf_list_t *list) {
    int i;
    conf_entry_t *ep = NULL;

#define print_branches(x) for (i = x;i;i--) printf("| ");
    LIST_FOREACH(ep, list, lp) {
        print_branches(depth);
        if (ep->type == CONF_TYPE_DATA)
            printf("%s-> %s = '%s'\n", (depth ? "\b" : ""), ep->name,
                    ep->string);
        else {
            printf("%s->[%s]\n", (depth ? "\b" : ""), ep->name);
            conf_display_tree(depth + 1, ep->list);
        }
    }
#undef print_branches
}

/* this function finds the first occurence of the conf with the given name (and
 * possibly containing data, if data is non-NULL) and of the given type (if
 * specified, it may be 0 if either a list or entry is acceptable) from the
 * given list, recursing at most maxdepth - 1 times. */
conf_entry_t *conf_find(const char *name, const char *data, int type,
    conf_list_t *list, int maxdepth) {
    conf_entry_t *cep = NULL, *tmpcep;

    if (maxdepth < 1 || list == NULL)
        return NULL;

    LIST_FOREACH(cep, list, lp) {
        if (!strcasecmp(cep->name, name) && (cep->type == type || type == 0)) {
            /* return the entry if the strings match, or if data is NULL */
            if (data == NULL)
                return cep;
            else if (!strcasecmp(cep->string, data))
                return cep;
        } else if (cep->type == CONF_TYPE_LIST) {
            if ((tmpcep = conf_find(name, data, type, cep->list, maxdepth - 1))
                    != NULL)
                return tmpcep;
        }
    }

    return NULL;
}

/* this function behaves exactly like the above, except that it finds the
 * specified entry *after* 'last'.  If last is NULL, it starts from the head of
 * the list. */
conf_entry_t *conf_find_next(const char *name, const char *data, int type,
        conf_entry_t *last, conf_list_t *list, int maxdepth) {
    conf_entry_t *cep = NULL, *tmpcep;

    if (maxdepth < 1 || list == NULL)
        return NULL;

    /* with a conf entry, we get the list data too, so we know where
     * to start from! */
    if (last == NULL)
        cep = LIST_FIRST(list);
    else
        cep = LIST_NEXT(last, lp);

    while (cep != NULL) {
        if (!strcasecmp(cep->name, name) &&
                (cep->type == type || type == 0)) {
            /* return the entry if the strings match, or if data is NULL */
            if (data == NULL)
                return cep;
            else if (!strcasecmp(cep->string, data))
                return cep;
        } else if (cep->type == CONF_TYPE_LIST) {
            if ((tmpcep = conf_find(name, data, type, cep->list, maxdepth - 1))
                    != NULL)
                return tmpcep;
        }
        cep = LIST_NEXT(cep, lp);
    }
    return NULL;
}

/* this is shorthand to find a list-type conf, it uses conf_find() above. */
conf_list_t *conf_find_list(const char *name, conf_list_t *list,
        int maxdepth) {
    conf_entry_t *cep;
   
    if ((cep = conf_find(name, NULL, CONF_TYPE_LIST, list, maxdepth)) != NULL)
        return cep->list;

    return NULL;
}

/* this is shorthand to find an entry-type conf, it uses conf_find() above. */
char *conf_find_entry(const char *name, conf_list_t *list, int maxdepth) {
    conf_entry_t *cep;
   
    if ((cep = conf_find(name, NULL, CONF_TYPE_DATA, list, maxdepth)) != NULL)
        return cep->string;

    return NULL;
}

/* these two functions work like conf_find_next(), only they're not as
 * efficient as they always have to start at the head of the conf.  If
 * possible, it is recommended you use conf_find_next() instead. */
conf_list_t *conf_find_list_next(const char *name, conf_list_t *last,
        conf_list_t *list, int maxdepth) {
    conf_entry_t *cep = NULL;
    conf_list_t *clp = NULL;
    int found = 0;

    if (maxdepth < 1 || list == NULL)
        return NULL;

    if (last == NULL)
        found = 1; /* if last is NULL, start at the beginning */

    LIST_FOREACH(cep, list, lp) {
        if (!found) {
            if (cep->list == last)
                found++; /* this is the matching entry */
            continue;
        }

        if (cep->type == CONF_TYPE_LIST && !strcasecmp(cep->name, name))
            return cep->list;
        else if (cep->type == CONF_TYPE_LIST) {
            if ((clp = conf_find_list(name, cep->list, maxdepth - 1)) != NULL)
                return clp;
        }
    }

    return NULL;
}
char *conf_find_entry_next(const char *name, char *last,
        conf_list_t *list, int maxdepth) {
    conf_entry_t *cep = NULL;
    char *s = NULL;
    int found = 0;

    if (maxdepth < 1 || list == NULL)
        return NULL;

    if (last == NULL)
        found = 1; /* if last is NULL, start at the beginning */

    LIST_FOREACH(cep, list, lp) {
        if (!found) {
            if (cep->string == last)
                found++; /* this is the matching entry */
            continue;
        }

        if (cep->type == CONF_TYPE_DATA && !strcasecmp(cep->name, name))
            return cep->string;
        else if (cep->type == CONF_TYPE_LIST) {
            if ((s = conf_find_entry(name, cep->list, maxdepth - 1)) != NULL)
                return s;
        }
    }

    return NULL;
}

/* this function prunes the data in 'oldstr', it clears out excess whitespace,
 * and handles reading in included files.  It also does light sanity-checking
 * to make sure comments and strings are properly terminated. */
static char *conf_preparse(char *file, char *oldstr) {
    char *s = oldstr;
    char *newstr = malloc(strlen(oldstr) + 1);
    char *str = newstr;
    int instring = 0;
    int incomment = 0;
    int line = 1, begline = 0;

    while (*s) {
        switch (*s) {
            case '"':
                if (incomment) {
                    s++;
                    break;
                }
                if (!instring)
                    begline=line;
                instring ^= 1;
                *str++ = *s++;
                break;
            case '\\': /* the \ just negates us from checking the next
                          character, shrug. */
                if (incomment) {
                    s++;
                    break;
                }
                *str++ = *s++;
                if (*s == '\n') {
#if 0
                    parse_err("cannot backquote literal newline.");
                    free(newstr);
                    free(oldstr);
                    return NULL;
#endif
                    line++;
                }
                *str++ = *s++;
                break;
            case '#':
                if (incomment) {
                    s++;
                    break;
                } else if (instring)
                    *str++ = *s++;
                else
                    while (*s && *s != '\n')
                        s++; /* a #-style comment.  whee. */
                break;
            case '/':
                if (incomment) {
                    s++;
                    break;
                }
                if (*(s + 1) == '/' && !instring) {
                    /* this being a comment of the C++ neature, eat until
                       EOL */
                    while (*s && *s != '\n') 
                        s++;
                } else if (*(s + 1) == '*' && !instring) {
                    s += 2;
                    incomment = 1;
                    begline=line;
                } else
                    *str++ = *s++;
                break;                
            case '\n':
                line++;
#if 0
                if (instring) {
                    parse_err("unterminated string on previous line");
                    free(newstr);
                    free(oldstr);
                    return NULL;
                }
#endif
                *str++ = *s++;
                break;
            default:
                if (!incomment)
                    *str++ = *s++;
                else if (incomment && *s == '*') {
                    if (*(s + 1) == '/') { /* the comment is over */
                        s += 2;
                        incomment=0;
                    } else
                        s++;
                } else
                    s++;
                break;
        }
    }

    *str = 0;
    free(oldstr);

    if (instring || incomment) {
        line = begline;
        parse_err("unterminated string or comment beginning here");
        free(newstr);
        return NULL;
    }
    
    return newstr;
}

/* do the final parsing of the data, extract name/value pairs and build our
 * tree, and all with a spot of recursion.  yum! */
static conf_list_t *conf_parse(char *file, int line, char *str) {
    conf_list_t *list = malloc(sizeof(conf_list_t));
    conf_entry_t *ent = NULL, *last = NULL;
    char *s, save;
    int i; /* use s as a place holder, and i as a length counter */
    int startline = 0;

#define swallow_whitespace() do {                                        \
    while (*str) {                                                        \
        if (*str == '\n')                                                \
            line++;                                                        \
        if (isspace(*str))                                                \
            str++;                                                        \
        else                                                                \
            break;                                                        \
    }                                                                        \
} while (0)

    LIST_INIT(list);

    while (*str) {
        swallow_whitespace();
        if (!*str)
            return list; /* that's it, we're done! */
        
        ent = malloc(sizeof(conf_entry_t));
        memset(ent, 0, sizeof(conf_entry_t));
        ent->parent = list;

        /* here's some cute syntactic sugar.  we allow nameless entries if
         * they are lists (start with/end with {/}) or if the are quoted (""),
         * we set the name field to "" */
        if (*str != '"' && *str != '{') {
            i = 0;
            s = str;
            while (*str && !isspace(*str) && *str != '{' && *str != ';') {
                str++;
                i++;
            }

            if (!*str) {
                destroy_conf_branch(list);
                parse_err("file terminated prematurely");
                return NULL; /* this is obviously eroneous */
            }
        
            /* silly hack for nameless entries here, too.  We swallow
             * whitespace, and if our next character is a ';', we craftily push
             * our pointer back to where it was before and let the parser
             * continue. */
            swallow_whitespace();
            if (*str == ';') {
                str = s;
                ent->name = malloc(1);
                strcpy(ent->name, "");
            } else {
                ent->name = malloc((size_t)i + 1);
                strncpy(ent->name, s, (size_t)i);
                ent->name[i]=0;
            }
        } else {
            ent->name = malloc(1);
            strcpy(ent->name, "");
        }
    
        swallow_whitespace();

        s = str;
        while (*str && *str != ';' && *str != '{') {
            if (*str == '"') {
                while (*str) {
                    if (*str == '\\')
                        str += 2;
                    else if (*str == '"') {
                        str++;
                        break;
                    } else
                        str++;
                }

                if (!*str) {
                    free(ent->name);
                    free(ent);
                    destroy_conf_branch(list);
                    parse_err("syntax error");
                    return NULL;
                }
            } else
                str++;
        }

        if (!*str) {
            free(ent->name);
            free(ent);
            destroy_conf_branch(list);
            parse_err("syntax error");
            return NULL;
        }

        /* copy in the string, if any.  if the string evaluates to empty, free
         * the memory and set string to NULL. */
        save = *str;
        *str = 0;
        ent->string = conf_expand_text(s);
        if (*ent->string == '\0') {
            free(ent->string);
            ent->string = NULL;
        }
        *str = save;

        /* if this really was a data-type entry, check to see if it was an
         * include statement, and do the including.  also check to see if it
         * boiled down to an empty string and complain if it did. */
        if (*str == ';') {
            ent->type = CONF_TYPE_DATA;
            if (ent->string == NULL) {
                /* actually.. let's try this.. if string came out NULL, make it
                 * a nameless entry. */
                ent->string = ent->name;
                ent->name = strdup("");
            }

            if (!strcmp(ent->name, "$INCLUDE")) {
                conf_list_t *inclist;

                if ((inclist = read_conf(ent->string)) == NULL) {
                    parse_err("error in included file:");
                    parse_err(ent->string);
                    destroy_conf_branch(list);
                    return NULL;
                } else {
                    free(ent->string);
                    free(ent);
                    last = merge_into_conf(list, last, inclist);
                }
            } else {
                if (last == NULL)
                    LIST_INSERT_HEAD(list, ent, lp);
                else
                    LIST_INSERT_AFTER(last, ent, lp);
                last = ent;
            }
            str++; /* skip the semicolon */
        } else if (*str == '{') {
            /* otherwise, it's a section, keep going .. */
            int depth = 1;
            startline = line;
            s = ++str;
            ent->type = CONF_TYPE_LIST;

            while (*str) {
                if (*str == '"') {
                    str++;
                    while (*str) {
                        if (*str == '\\' && *(str + 1) != '\0')
                            str += 2;
                        else if (*str == '"')
                            break;
                        else
                            str++;
                    }
                } else if (*str == '{')
                    depth++;
                else if (*str == '}') {
                    depth--;

                    if (depth == 0)
                        break;
                }
                if (*str == '\n')
                    line++;
                str++;
            }

            if (!*str) {
                free(ent->name);
                free(ent);
                destroy_conf_branch(list);
                parse_err("syntax error (unclosed braces)");
                return NULL; /* some kind of syntax error */
            }
            
            *str++ = 0;
            ent->list = conf_parse(file, startline, s);
            if (ent->list == NULL) {
                free(ent->name);
                free(ent);
                destroy_conf_branch(list);
                return NULL;
            }

            if (last == NULL)
                LIST_INSERT_HEAD(list, ent, lp);
            else
                LIST_INSERT_AFTER(last, ent, lp);
            last = ent;

            swallow_whitespace();

            if (!*str) {
                free(ent->name);
                free(ent);
                destroy_conf_branch(list);
                parse_err("missing semicolon (';')");
                return NULL; /* missing semi-colon */
            } else
                s = NULL;

            if (*str != ';') {
                free(ent->name);
                destroy_conf_branch(ent->list);
                free(ent);
                parse_err("garbage between closing brace ('}') and "
                       "semicolon (';')");
                return NULL;
            }
            
            *str++ = '\0';
            continue;
        } else {
            destroy_conf_branch(list);
            parse_err("conf parser barfed!");
            return NULL; /* some kind of syntax error */
        }
    }

    return list; /* finished! */
}

/* merge everything in the conf_list 'little' into the conf_list 'big',
 * place entries starting after the 'after' point */
static conf_entry_t *merge_into_conf(conf_list_t *big, conf_entry_t *after,
        conf_list_t *little) {
    conf_entry_t *last = after;
    conf_entry_t *ep;

    while (!LIST_EMPTY(little)) {
        ep = LIST_FIRST(little);
        LIST_REMOVE(ep, lp);
        if (last == NULL)
            LIST_INSERT_HEAD(big, ep, lp);
        else
            LIST_INSERT_AFTER(last, ep, lp);
        ep->parent = big;
        last = ep;
    }

    free(little);
    return last;
}

/* this is a small function used to clean up strings read by the conf parser.
 * basically, what we do is trim whitespaces down to a single space unless the
 * entry is quoted,  It also trims unquoted whitespace from the end of the
 * string.  Lastly, for certain backquoted sequences in quoted text the
 * sequences are expanded to their C equivalents (\n, \r, et al) */
static char *conf_expand_text(char *str) {
    char *newstr = malloc(strlen(str) + 1);
    char *s = newstr;
    
    while (*str) {
        if (*str == '"') {
            str++;
            while (*str) {
                if (*str == '\\') {
                    str++;
                    if (*str == '\0') {
                        *s++ = '\\';
                        break;
                    }
                    switch (*str) {
                        case 'a':
                            *s++ = '\007'; /* BEL character (^G) */
                            break;
                        case 'b':
                            *s++ = '\010'; /* Backspace character (^H) */
                            break;
                        case 'f':
                            *s++ = '\014'; /* Form-feed character (^L) */
                            break;
                        case 'n':
                            *s++ = '\012'; /* Newline character (^J) */
                            break;
                        case 'r':
                            *s++ = '\015'; /* Carriage return character (^M) */
                            break;
                        case 't':
                            *s++ = '\011'; /* Tab character (^I) */
                            break;
                        case 'v':
                            *s++ = '\013'; /* Vertical tab character (^K) */
                            break;
                        default:
                            *s++ = *str;
                    }
                    str++;
                } else if (*str == '"') {
                    str++;
                    break; /* break out of the inner loop, the outer one will
                              pick up the slack. */
                } else
                    *s++ = *str++;
            }
        } else if (isspace(*str)) {
            *s++ = ' ';
            while (isspace(*str))
                str++;
            if (!*str)
                s--; /* let that last space get trimmed down below. */
        } else 
            *s++ = *str++;
    }
    
    *s = 0;
    return newstr;
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
