#!/bin/sh

# This is a small script used to walk through the modules directory and
# generate dependencies for all files which request them.

from="$1"
to="$2"

# DO NOT CHANGE THIS UNLESS YOU MODIFY ALL FILES TO USE THE NEW WORD!
depword="@DEPENDENCIES@:"

rm -f $to
for mod in `grep "$depword" $from | sed "s/^${depword}//"` ; do
    echo $mod >> $to
done

