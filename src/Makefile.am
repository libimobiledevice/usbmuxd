AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = $(GLOBAL_CFLAGS) $(libplist_CFLAGS)
AM_LDFLAGS = $(GLOBAL_LIBS) $(libpthread_LIBS) $(libplist_LIBS)

lib_LTLIBRARIES = libusbmuxd.la
libusbmuxd_la_LDFLAGS = $(AM_LDFLAGS) -version-info $(LIBUSBMUXD_SO_VERSION) -no-undefined
libusbmuxd_la_LIBADD =
libusbmuxd_la_SOURCES = \
		collection.c collection.h \
		socket.c socket.h \
		libusbmuxd.c

if WIN32
libusbmuxd_la_LDFLAGS += -avoid-version
libusbmuxd_la_LIBADD += -lws2_32
endif
