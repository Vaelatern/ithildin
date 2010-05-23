#!/bin/sh

# try to get the version string.  it will either be a standalone string or a
# string of the form "$Revision: 578 $".  We try the latter first.
VSTR=`grep 'MODULE_REGISTER' $1 | sed 's/.*\"\(.*\)\".*/\1/'`
ESTR=`echo $VSTR | sed 's/\$Rev: \(.*\) \$$/\1/'`
if test -n "$ESTR" ; then
    VSTR=$ESTR
fi

# now we need to make sure the string doesn't have any spaces or tabs.  Just
# turn them both into dashes.
VSTR=`echo $VSTR | tr "\t " "--"`
echo $VSTR
