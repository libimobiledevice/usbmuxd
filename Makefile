TARGETS=usbmuxd iproxy libusbmuxd.so
CFLAGS=-I. -Wall -g -DDEBUG -fPIC
LIBS=-lpthread -lusb -lrt
LDFLAGS=-L.
INSTALL_PREFIX=/usr/local

all:	$(TARGETS)

main.o: main.c usbmuxd-proto.h sock_stuff.h iphone.h
iphone.o: iphone.c iphone.h usbmuxd.h sock_stuff.h
sock_stuff.o: sock_stuff.c sock_stuff.h
libusbmuxd.o: libusbmuxd.c usbmuxd.h usbmuxd-proto.h
iproxy.o: iproxy.c sock_stuff.h
libusbmuxd.so: libusbmuxd.o sock_stuff.o

%.so:	%.o
	$(CC) -o $@ -shared -Wl,-soname,$@.1 $^

%.o:    %.c
	$(CC) -o $@ $(CFLAGS) -c $< 

usbmuxd: main.o sock_stuff.o iphone.o
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

iproxy: iproxy.o
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBS) -lusbmuxd

clean:
	rm -f *.o *.so $(TARGETS)

realclean: clean
	rm -f *~

install: all
	install -m 755 usbmuxd $(INSTALL_PREFIX)/sbin/
	# udev crack
	#install -D -m 644 85-usbmuxd.rules $(INSTALL_PREFIX)/lib/udev/rules.d/85-usbmuxd.rules
	install -m 644 85-usbmuxd.rules /etc/udev/rules.d/85-usbmuxd.rules
	# protocol
	install -m 644 usbmuxd-proto.h $(INSTALL_PREFIX)/include/
	# iproxy
	install -m 644 libusbmuxd.so $(INSTALL_PREFIX)/lib/
	install -m 644 usbmuxd.h $(INSTALL_PREFIX)/include/
	install -m 755 iproxy $(INSTALL_PREFIX)/bin/

uninstall:
	-rm $(INSTALL_PREFIX)/sbin/usbmuxd
	#-rm $(INSTALL_PREFIX)/lib/udev/rules.d/85-usbmuxd.rules
	-rm /etc/udev/rules.d/85-usbmuxd.rules
	-rm $(INSTALL_PREFIX)/include/usbmuxd-proto.h
	-rm $(INSTALL_PREFIX)/lib/libusbmuxd.so
	-rm $(INSTALL_PREFIX)/include/usbmuxd.h
	-rm $(INSTALL_PREFIX)/bin/iproxy

.PHONY: all clean realclean
