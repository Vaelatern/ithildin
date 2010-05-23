###############################################################################
#
# ircd/commands/Makefile: makefile for the ircd commands
#
# Copyright 2002 the Ithildin Project.
# See the COPYING file for more information on licensing and use.
#
# $Id: Makefile 583 2005-08-21 06:42:56Z wd $
#
###############################################################################

SOURCES := $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.so)

INCLUDES = -I../../../include -I..

all: $(OBJECTS)
	
install:
	mkdir -p $(LIBDIR)/ircd/commands
	@echo ">>> installing command modules in $(LIBDIR)/ircd/commands"
	@$(foreach obj,$(OBJECTS), \
	    $(INSTALL) $(obj) $(LIBDIR)/ircd/commands ;\
	    sh ../../../tools/gendeps.sh $(obj:.so=.c) \
		$(LIBDIR)/ircd/commands/$(obj:.so=.deps) ;)

%.so: %.c
	$(CCDLL) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $(<:.c=.so) $<

# file dependencies
%.d: %.c
	@set -e; $(CC) -M $(CFLAGS) $(INCLUDES) $< \
	    | sed 's/\($*\)\.o[ :]*/\1.o $@ : /g' > $@; \
	    test -s $@ || rm -f $@
-include $(SOURCES:.c=.d)

# vi:set ts=8 sts=4 sw=4 tw=76:
