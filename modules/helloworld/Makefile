# A simple, small makefile.  All of our presets have already been set for
# us, all we have to do is compile the relevant files.
#
# $Id: Makefile 583 2005-08-21 06:42:56Z wd $

SOURCES = helloworld.c
OBJECTS = $(SOURCES:.c=.o)

INCLUDES = -I../../include

all: $(OBJECTS)
	$(CCDLL) $(LDFLAGS) -o helloworld.so $(OBJECTS)

install:
	$(INSTALL) helloworld.so $(LIBDIR)
	sh ../../tools/gendeps.sh helloworld.c $(LIBDIR)/helloworld.deps

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -I../nbdns -I../ident -c $<

# vi:set ts=8 sts=4 sw=4 tw=76:
