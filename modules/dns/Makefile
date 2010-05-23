# A simple, small makefile.  All of our presets have already been set for
# us, all we have to do is compile the relevant files.
#
# $Id: Makefile 583 2005-08-21 06:42:56Z wd $

SOURCES = dns.c lookup.c packet.c res_comp.c
OBJECTS = $(SOURCES:.c=.o)

INCLUDES = -I../../include -I.

all: $(OBJECTS)
	$(CCDLL) $(LDFLAGS) -o dns.so $(OBJECTS)

install:
	$(INSTALL) dns.so $(LIBDIR)
	sh ../../tools/gendeps.sh dns.c $(LIBDIR)/dns.deps
	@if ! test -e $(CONFDIR)/dns.conf ; then\
	    echo ">>> installing dns configuration files";\
	    mkdir -p $(CONFDIR);\
	    $(INSTALL) -m 644 conf/dns.conf $(CONFDIR);\
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
