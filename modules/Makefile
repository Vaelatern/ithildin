###############################################################################
#
# modules/Makefile.in: make file for the modules directory.  Any tweaking
# should be done in ../Makefile as far as compiler flags/etc, or done with
# configure.
#
# Copyright 2002 the Ithildin Project.
# See the COPYING file for more information on licensing and use.
#
# $Id: Makefile.in 583 2005-08-21 06:42:56Z wd $
#
###############################################################################

CC = gcc
CFLAGS = -g -O2  -fPIC -DPIC -shared
LDFLAGS =  -Wl,-export-dynamic -shared -nostdlib
CCDLL = gcc
LIBS = -lnsl -ldl  -lssl -lcrypto
CTAGS = @CTAGS@

prefix = /home/me/a/ithildin-1.1-dev
exec_prefix = ${prefix}
INSTALL = /usr/bin/install -c
LIBDIR = ${exec_prefix}/lib/ithildin
DATADIR = ${prefix}/share/ithildin
CONFDIR = ${prefix}/etc/ithildin

REPOVER ?= 0

MFLAGS='CC=$(CC)'							\
    'CFLAGS=$(CFLAGS)'							\
    'LDFLAGS=$(LDFLAGS)'						\
    'CCDLL=$(CCDLL)'							\
    'LIBS=$(LIBS)'							\
    'prefix=$(prefix)'							\
    'exec_prefix=$(exec_prefix)'					\
    'INSTALL=$(INSTALL)'						\
    'LIBDIR=$(LIBDIR)'							\
    'DATADIR=$(DATADIR)'						\
    'CONFDIR=$(CONFDIR)'

# the different modules to build (each has its own Makefile, too)
MODULES = *

default: all

all:
	@echo ">>> building modules ..."
	@set -e ; for mod in $(MODULES) ; do				\
	    if test -d $$mod -a -f $$mod/Makefile ; then		\
		echo ">>> building $${mod}";				\
		(cd $${mod} && $(MAKE) $(MFLAGS)) ;			\
		echo "<<< finished building $${mod}";			\
	    fi								\
	done
		
install:
	@echo ">>> installing modules in $(LIBDIR)"
	-@if test -d $(LIBDIR).old ; then				\
	    echo "rm -rf $(LIBDIR).old";				\
	    rm -rf $(LIBDIR).old;					\
	fi
	-@if test -d $(LIBDIR); then					\
	    echo "mv $(LIBDIR) $(LIBDIR).old";				\
	    mv $(LIBDIR) $(LIBDIR).old;					\
	fi
	-@if test ! -d $(LIBDIR); then					\
	    echo "mkdir -p $(LIBDIR)";					\
	    mkdir -p  $(LIBDIR);					\
	fi
	@set -e ; for mod in $(MODULES) ; do				\
	    if test -d $$mod -a -f $$mod/Makefile ; then		\
		echo ">>> installing $${mod}";				\
		(cd $${mod} && $(MAKE) $(MFLAGS) install) ;		\
	    fi								\
	done

clean:
	@echo ">>> cleaning modules ..."
	find . -name '*.[do]' | xargs rm -f
	find . -name '*.so' | xargs rm -f

release:
	@echo ">>> building module releases ..."
	@mkdir -p ../dist/$(REPOVER)/modules/
	@set -e ; for mod in $(MODULES) ; do				\
	    if test -d $$mod -a -f $$mod/Makefile ; then		\
		ver="";							\
		if test -e $$mod/$$mod.c ; then				\
		    ver="-`sh ../tools/modver.sh $$mod/$$mod.c`";	\
		fi;							\
		echo ">>> building release for $${mod}$${ver} in dist/$(REPOVER)/modules/";\
		tar -zcf ../dist/$(REPOVER)/modules/$${mod}$$ver.tar.gz $${mod};\
	    fi								\
	done

# vi:set ts=8 sts=4 sw=4 tw=76:
