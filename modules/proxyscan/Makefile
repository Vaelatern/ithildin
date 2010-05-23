# A simple, small makefile.  All of our presets have already been set for
# us, all we have to do is compile the relevant files.
#
# $Id: Makefile 583 2005-08-21 06:42:56Z wd $

SOURCES = proxyscan.c
OBJECTS = $(SOURCES:.c=.o)

INCLUDES = -I../../include -I.

all: $(OBJECTS)
	$(CCDLL) $(LDFLAGS) -o proxyscan.so $(OBJECTS)

install:
	$(INSTALL) proxyscan.so $(LIBDIR)
	sh ../../tools/gendeps.sh proxyscan.c $(LIBDIR)/proxyscan.deps

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

# vi:set ts=8 sts=4 sw=4 tw=76:
