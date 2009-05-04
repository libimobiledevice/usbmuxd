AM_CFLAGS = $(GLOBAL_CFLAGS) $(libusb_CFLAGS)
AM_LDFLAGS = $(libusb_LIBS) -lpthread -lrt

# Libraries

noinst_LTLIBRARIES = libusbmux.la libsock_stuff.la
libsock_stuff_la_SOURCES = sock_stuff.c \
			   sock_stuff.h

libusbmux_la_SOURCES = usbmux.c \
		       usbmux.h
libusbmux_la_CFLAGS = $(AM_CFLAGS)
libusbmux_la_LDFLAGS = $(AM_LDFLAGS)

lib_LTLIBRARIES = libusbmuxd.la
libusbmuxd_la_SOURCES = libusbmuxd.c \
		        usbmuxd.h \
		        usbmuxd-proto.h
libusbmuxd_la_LIBADD = libsock_stuff.la

include_HEADERS = usbmuxd.h \
		  usbmuxd-proto.h

# Programs

sbin_PROGRAMS = usbmuxd

usbmuxd_SOURCES = main.c
usbmuxd_LDADD = libusbmux.la \
	        libsock_stuff.la

