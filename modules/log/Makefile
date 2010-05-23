# A simple, small makefile.  All of our presets have already been set for
# us, all we have to do is compile the relevant files.
#
# $Id: Makefile 583 2005-08-21 06:42:56Z wd $

SOURCES = log.c
OBJECTS = $(SOURCES:.c=.o)

INCLUDES = -I../../include -I.

all: $(OBJECTS)
	$(CCDLL) $(LDFLAGS) -o log.so $(OBJECTS)

install:
	$(INSTALL) log.so $(LIBDIR)
	sh ../../tools/gendeps.sh log.c $(LIBDIR)/log.deps
	@if ! test -e $(CONFDIR)/log.conf ; then\
	    echo ">>> installing log configuration files";\
	    mkdir -p $(CONFDIR);\
	    $(INSTALL) -m 644 conf/log.conf $(CONFDIR);\
	fi

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $(<:.c=.o)

# file dependencies
%.d: %.c
	@set -e; $(CC) -M $(CFLAGS) $(INCLUDES) $< \
	    | sed 's/\($*\)\.o[ :]*/\1.o $@ : /g' > $@; \
	    test -s $@ || rm -f $@
-include $(SOURCES:.c=.d)

# vi:set ts=8 sts=4 sw=4 tw=76:
