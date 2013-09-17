AM_CFLAGS = $(GLOBAL_CFLAGS) -I$(top_srcdir)/src $(libplist_CFLAGS) $(libusb_CFLAGS)
AM_LDFLAGS = $(libplist_LIBS) $(libusb_LIBS)

bin_PROGRAMS = usbmuxd

usbmuxd_SOURCES = client.c client.h \
		device.c device.h \
		log.c log.h \
		usb-linux.c usb.h \
		utils.c utils.h \
		main.c
usbmuxd_CFLAGS = $(AM_CFLAGS)
usbmuxd_LDFLAGS = $(AM_LDFLAGS)

