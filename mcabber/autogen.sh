#!/bin/bash

libtoolize --force --automake
aclocal -I macros/
autoheader
autoconf
automake -a --copy
