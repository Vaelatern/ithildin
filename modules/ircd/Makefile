###############################################################################
#
# ircd/Makefile: makefile for the ircd module
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

SOURCES = channel.c class.c client.c command.c conf.c connection.c	\
	  ircd.c ircstring.c privilege.c protocol.c send.c server.c	\
	  support.c 
OBJECTS = $(SOURCES:.c=.o)

SUBDIRS = addons commands protocols

all: $(OBJECTS)
	$(CCDLL) $(LDFLAGS) -o ircd.so $(OBJECTS)
	@set -e ; for subdir in $(SUBDIRS) ; do				\
	    echo ">>> building modules for ircd/$${subdir}";		\
	    (cd $${subdir} && $(MAKE) $(MFLAGS));			\
	done
	
install:
	$(INSTALL) ircd.so $(LIBDIR)
	sh ../../tools/gendeps.sh ircd.c $(LIBDIR)/ircd.deps
	@if ! test -d $(LIBDIR)/ircd; then				\
	    mkdir -p $(LIBDIR)/ircd;					\
	fi
	@set -e ; for subdir in $(SUBDIRS) ; do				\
	    echo ">>> installing modules for ircd/$${subdir}";		\
	    (cd $${subdir} && $(MAKE) $(MFLAGS) install);		\
	done
	@echo ">>> installing ircd documentation ..."
	@mkdir -p $(DATADIR)/doc/ircd
	@cp -Rp doc/* $(DATADIR)/doc/ircd
	@if ! test -e $(CONFDIR)/ircd ; then				\
	    echo ">>> installing ircd configuration files";		\
	    mkdir -p $(CONFDIR)/ircd;					\
	    $(INSTALL) -b -m 644 conf/*.conf conf/*motd $(CONFDIR)/ircd;\
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
