#!/bin/sh
if [ ! -d m4 ]; then echo mkdir m4; fi
aclocal -I m4 || exit 1
libtoolize || exit 1
autoheader || exit 1
automake --add-missing || exit 1
autoconf || exit 1
