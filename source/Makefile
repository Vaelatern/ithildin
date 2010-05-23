###############################################################################
#
# source/Makefile.in: make file for the source directory.  Any tweaking should
# be done in ../Makefile as far as compiler flags/etc, or done with configure.
#
# Copyright 2002 the Ithildin Project.
# See the COPYING file for more information on licensing and use.
#
# $Id: Makefile.in 582 2005-08-21 06:42:46Z wd $
#
###############################################################################

prefix = /home/me/a/ithildin-1.1-dev
exec_prefix = ${prefix}

CC = gcc
CCDLL = gcc
CFLAGS = -g -O2
CFLAGSDLL = -g -O2  -fPIC -DPIC -shared
LDFLAGS =  -Wl,-export-dynamic
LDFLAGSDLL =  -Wl,-export-dynamic -shared -nostdlib
CTAGS = @CTAGS@
LIBS = -lnsl -ldl  -lssl -lcrypto
CONFPATH = ${prefix}/etc/ithildin
LIBPATH = ${exec_prefix}/lib/ithildin
DATAPATH = ${prefix}/share/ithildin
INCLUDES = -I../include -I.
PACKAGE = ithildin
REPOVER ?= 0

# the source files
SOURCES = conf.c event.c hash.c log.c main.c md5.c module.c	\
	  socket.c string.c timer.c util.c
OBJECTS = $(SOURCES:.c=.o)

default: all

all: $(OBJECTS) md5sum
	$(CC) $(LDFLAGS) -o $(PACKAGE) $(OBJECTS) $(LIBS)
	ls -l $(PACKAGE)

# generic compile ruleset
.c.o: 
	$(CC) $(CFLAGS) $(INCLUDES) -c $<
# special stuff:
main.o:
	$(CC) $(CFLAGS) $(INCLUDES) -DCONF_PATH="\"$(CONFPATH)\"" -DLIB_PATH="\"$(LIBPATH)\"" -DDATA_PATH="\"$(DATAPATH)\"" -DCOMP_FLAGS="\"$(CFLAGS)\"" -DCOMP_FLAGS_MOD="\"$(CFLAGSDLL)\"" -DCOMP_LDFLAGS="\"$(LDFLAGS)\"" -DCOMP_LDFLAGS_MOD="\"$(LDFLAGSDLL)\"" -DREPOVER=$(REPOVER) -c main.c

md5sum: md5sum.o
	$(CC) $(LDFLAGS) -o md5sum md5sum.o md5.o $(LIBS)

# file dependencies
%.d: %.c
	@set -e; $(CC) -M $(CFLAGS) $(INCLUDES) $< \
	    | sed 's/\($*\)\.o[ :]*/\1.o $@ : /g' > $@; \
	    test -s $@ || rm -f $@
-include $(SOURCES:.c=.d)

# vi:set ts=8 sts=4 sw=4 tw=76:
