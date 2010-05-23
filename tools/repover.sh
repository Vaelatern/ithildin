#!/bin/sh
#
# $Rev: 851 $

SVN=`which svn`

rev=""
# if subversion is available and we have a .svn directory get the repo
# version directly from it
if test "$SVN" != "" -a -d ".svn" ; then
    rev=`$SVN info . | grep '^Revision' | sed 's/Revision: //'`
fi

if test "$rev" = "" ; then
    rev=`cat tools/repover.sh | grep '^# \$Rev:' |                        \
        sed 's/^# \$Rev: \(.*\) \$$/\1/'`
fi
if test "$rev" = "" ; then
    rev=0
else
    cat tools/repover.sh | sed "s/Rev: .*\\\$$/Rev: $rev \\\$/" > repover.tmp
    mv repover.tmp tools/repover.sh
fi
echo $rev

