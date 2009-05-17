SUBDIRS = src udev tools

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libusbmuxd.pc

indent:
	indent -kr -ut -ts4 -l120 src/*.c src/*.h tools/*.c

EXTRA_DIST = README.devel
