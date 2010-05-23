###############################################################################
#
# Makefile.in: Top-level makefile for ithildin.
#
# Copyright 2002 the Ithildin Project.
# See the COPYING file for more information on licensing and use.
#
# $Id: Makefile.in 802 2007-03-22 04:13:51Z wd $
#
###############################################################################

# these are where to install things at the end of the day
prefix = /home/me/a/ithildin-1.1-dev
exec_prefix = ${prefix}
BINDIR = ${exec_prefix}/bin
DATADIR = ${prefix}/share/ithildin
CONFDIR = ${prefix}/etc/ithildin
INCDIR = ${prefix}/include/ithildin
INSTALL = /usr/bin/install -c
PACKAGE = ithildin
VERSION = ithildin-1.1.2

# binaries which might prove useful
RM = rm
CTAGS = @CTAGS@

# the nitty-gritty compiler stuff.

CC = gcc
CFLAGS = -g -O2 -m32
LDFLAGS =  -Wl,-export-dynamic
LIBS = -lnsl -ldl  -lssl -lcrypto

# avoid directory printing
MAKEFLAGS += --no-print-directory

default: all

all: base_build modules_build

base_build:
	@echo ">>> building base system ..."
	@repover=`sh tools/repover.sh` ;				\
	 (cd source && $(MAKE) REPOVER=$$repover all)
	@echo "<<< finished building base system"

modules_build:
	@(cd modules && $(MAKE) all)

base_install: base_build
	mkdir -p $(BINDIR)
	$(INSTALL) -b source/$(PACKAGE) $(BINDIR)
	$(INSTALL) source/md5sum $(BINDIR)

conf_install:
	@if test -e $(CONFDIR)/$(PACKAGE).conf; then\
	    echo ">>> not overwriting conf files";\
	else\
	    echo ">>> installing configuration files";\
	    mkdir -p $(CONFDIR);\
	    $(INSTALL) -b -m 644 doc/conf/*.conf $(CONFDIR);\
	fi
	
data_install:
	mkdir -p $(DATADIR)/doc
	@echo ">>> installing data files in $(DATADIR)"
	@$(INSTALL) -m 644 COPYING DEVELOPERS README $(DATADIR)
	@echo ">>> installing documentation in $(DATADIR)/doc"
	@cd doc && for f in `find . \! -path '*/.svn*' -type f` ; do	 \
	    df=`dirname $$f`						;\
	    mkdir -p $(DATADIR)/doc/$$df				;\
	    $(INSTALL) -m 644 $$f $(DATADIR)/doc/$$df			;\
	done

include_install:
	mkdir -p $(INCDIR)
	@echo ">>> installing include files in $(INCDIR)"
	$(INSTALL) -m 644 include/*.h $(INCDIR)

modules_install: modules_build
	@cd modules; $(MAKE) install

install: base_install modules_install conf_install data_install include_install
	@echo ">>> well, there you have it.  everything installed.  enjoy!"

clean:
	@echo ">>> cleaning base"
	rm -f *~ doc/*~ include/*~ source/*~ *.core source/*.core
	rm -f source/*.[do] source/$(PACKAGE) source/md5sum
	@cd modules; $(MAKE) clean
	@echo "<<< finished cleaning"

distclean: clean
	rm -rf autom4te.cache config.log config.status configure.lineno
	rm -f tags source/tags include/config.h
	rm -f Makefile source/Makefile modules/Makefile doc/conf/Makefile
	rm -f `find . -name '*.d'`

update: clean
	svn update
	@cfgline=`grep -E '^  \\$$' config.log | head -1 |		\
		sed 's/^  \\$$ //' | sed 's/--quiet//'`			;\
	if test autoconf/configure.ac -nt configure ; then		\
	    echo ">>> updating configure and include/config.h.in"	;\
	    autoconf -o configure autoconf/configure.ac			;\
	    autoheader autoconf/configure.ac				;\
	    echo ">>> re-running configure as:"				;\
	    echo "    $$cfgline"					;\
	    $$cfgline							;\
	fi

reconf: reconfigure
reconfig: reconfigure
reconfigure:
	@cfgline=`grep -E '^  \\$$' config.log | head -1 | sed 's/^  \\$$ //'`;\
	echo ">>> re-running configure as:"				;\
	echo "    $$cfgline"						;\
	$$cfgline

DISTFILES = .svn COPYING DEVELOPERS Makefile.in README autoconf	\
	    configure doc include source tools
MDISTFILES = modules/.svn modules/Makefile.in

release: clean
	@repover=`sh tools/repover.sh` ;				\
	 echo ">>> building release for revision $$repover ..." ;	\
	 autoconf -o configure autoconf/configure.ac ;			\
	 autoheader autoconf/configure.ac ;				\
	 rm -rf dist ;							\
	 mkdir -p dist/$$repover ;					\
	 echo ">>> building release in $(VERSION).tar.gz";		\
	 mkdir dist/$$repover/$(VERSION) ;				\
	 cp -R $(DISTFILES) dist/$$repover/$(VERSION) ;			\
	 cp -R modules dist/$$repover/$(VERSION)/modules ;		\
	 (cd dist/$$repover && tar -zcf $(VERSION).tar.gz $(VERSION)) ;	\	
	 rm -rf dist/$$repover/$(VERSION) ;				\
	 echo ">>> building release for base system in $(VERSION)-base.tar.gz";\
	 mkdir dist/$$repover/$(VERSION) ;				\
	 cp -R $(DISTFILES) dist/$$repover/$(VERSION) ;			\
	 mkdir -p dist/$$repover/$(VERSION)/modules ;			\
	 cp -R $(MDISTFILES) dist/$$repover/$(VERSION)/modules ;	\
	 (cd dist/$$repover && tar -zcf $(VERSION)-base.tar.gz $(VERSION)) ;\
	 rm -rf dist/$$repover/$(VERSION) ;				\
	 (cd modules && $(MAKE) REPOVER=$$repover release) ;		\
	 echo "<<< finished building release in dist/$$repover"

# vi:set ts=8 sts=4 sw=4 tw=76:
