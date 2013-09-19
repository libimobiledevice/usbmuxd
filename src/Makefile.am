AM_CFLAGS = $(GLOBAL_CFLAGS) -I$(top_srcdir)/src $(libplist_CFLAGS) $(libusb_CFLAGS) $(libimobildevice_CFLAGS)
AM_LDFLAGS = $(libplist_LIBS) $(libusb_LIBS) $(libimobiledevice_LIBS) $(libpthread_LIBS)

sbin_PROGRAMS = usbmuxd

usbmuxd_SOURCES = client.c client.h \
		device.c device.h \
		preflight.c preflight.h \
		log.c log.h \
		usb-linux.c usb.h \
		utils.c utils.h \
		main.c
usbmuxd_CFLAGS = $(AM_CFLAGS)
usbmuxd_LDFLAGS = $(AM_LDFLAGS)

