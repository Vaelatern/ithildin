###############################################################################
#
# services/Makefile: makefile for the services module
#
# Copyright 2002 the Ithildin Project.
# See the COPYING file for more information on licensing and use.
#
# $Id: Makefile 583 2005-08-21 06:42:56Z wd $
#
###############################################################################

CC = $(CC)
CFLAGS = $(CFLAGS)
LDFLAGS = $(LDFLAGS)
CCDLL = $(CCDLL)
LIBS = $(LIBS)
INCLUDES = -I../../include -I.
LIBDIR = $(LIBDIR)
DATADIR = $(DATADIR)
CONFDIR = $(CONFDIR)

MFLAGS='CC=$(CC)'							\
    'CFLAGS=$(CFLAGS)'							\
    'LDFLAGS=$(LDFLAGS)'						\
    'CCDLL=$(CCDLL)'							\
    'LIBS=$(LIBS)'							\
    'prefix=$(prefix)'							\
    'exec_prefix=$(exec_prefix)'					\
    'INSTALL=$(INSTALL)'						\
    'LIBDIR=$(LIBDIR)'

SOURCES = conf.c db.c mail.c nick.c oper.c send.c services.c
OBJECTS = $(SOURCES:.c=.o)

all: $(OBJECTS)
	$(CCDLL) $(LDFLAGS) -o services.so $(OBJECTS)
	
install:
	$(INSTALL) services.so $(LIBDIR)
	sh ../../tools/gendeps.sh services.c $(LIBDIR)/services.deps
	@if ! test -e $(CONFDIR)/services.conf ; then			\
	    echo ">>> installing services configuration files";		\
	    mkdir -p $(CONFDIR);					\
	    $(INSTALL) -b -m 644 conf/*.conf $(CONFDIR);		\
	fi

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $(<:.c=.o)

# file dependencies
%.d: %.c
	@set -e; $(CC) -M $(CFLAGS) $(INCLUDES) $<			\
	    | sed 's/\($*\)\.o[ :]*/\1.o $@ : /g' > $@;			\
	    test -s $@ || rm -f $@
-include $(SOURCES:.c=.d)

# vi:set ts=8 sts=4 sw=4 tw=76:
