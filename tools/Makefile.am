AM_CFLAGS = $(GLOBAL_CFLAGS) -I$(top_srcdir)/src -I$(top_srcdir)/include
AM_LDFLAGS = $(libpthread_LIBS)

bin_PROGRAMS = iproxy

iproxy_SOURCES = iproxy.c
iproxy_CFLAGS = $(AM_CFLAGS)
iproxy_LDFLAGS = $(AM_LDFLAGS)
iproxy_LDADD = ../src/libusbmuxd.la

