###############################################################################
#
# ircd/addons/Makefile: makefile for the ircd addons
#
# Copyright 2002 the Ithildin Project.
# See the COPYING file for more information on licensing and use.
#
# $Id: Makefile 583 2005-08-21 06:42:56Z wd $
#
###############################################################################

SOURCES := $(if								\
	    $(shell							\
		if test -f ../../proxyscan/proxyscan.h ; then		\
		    echo 'pscan.c';					\
		fi),							\
	    $(wildcard *.c),						\
	    $(filter-out pscan.c,$(wildcard *.c)))
OBJECTS = $(SOURCES:.c=.so)

INCLUDES = -I../../../include -I..

all: $(OBJECTS)
	
install:
	mkdir -p $(LIBDIR)/ircd/addons
	@echo ">>> installing addon modules in $(LIBDIR)/ircd/addons"
	@$(foreach obj,$(OBJECTS), \
	    $(INSTALL) $(obj) $(LIBDIR)/ircd/addons ;\
	    sh ../../../tools/gendeps.sh $(obj:.so=.c) \
		$(LIBDIR)/ircd/addons/$(obj:.so=.deps) ;)

%.so: %.c
	$(CCDLL) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $(<:.c=.so) $<

# file dependencies
%.d: %.c
	@set -e; $(CC) -M $(CFLAGS) $(INCLUDES) $< \
	    | sed 's/\($*\)\.o[ :]*/\1.o $@ : /g' > $@; \
	    test -s $@ || rm -f $@
-include $(SOURCES:.c=.d)

# vi:set ts=8 sts=4 sw=4 tw=76:
