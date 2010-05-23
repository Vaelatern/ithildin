# A simple, small makefile.  All of our presets have already been set for
# us, all we have to do is compile the relevant files.
#
# $Id: Makefile 583 2005-08-21 06:42:56Z wd $

SOURCES = ident.c
OBJECTS = $(SOURCES:.c=.o)

INCLUDES = -I../../include -I.

all: $(OBJECTS)
	$(CCDLL) $(LDFLAGS) -o ident.so $(OBJECTS)

install:
	$(INSTALL) ident.so $(LIBDIR)
	sh ../../tools/gendeps.sh ident.c $(LIBDIR)/ident.deps

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

# vi:set ts=8 sts=4 sw=4 tw=76:
