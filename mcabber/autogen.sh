#! /bin/sh

libtoolize --force --automake --copy
aclocal -I macros/
autoheader
autoconf
automake -a --copy
