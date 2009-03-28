TARGETS=usbmuxd iproxy
CFLAGS=-I. -Wall -g -DDEBUG
LIBS=-lpthread -lusb -lrt
LDFLAGS=
INSTALL_PREFIX=/usr

all:	$(TARGETS)

main.o: main.c usbmuxd.h sock_stuff.h iphone.h
iphone.o: iphone.c iphone.h usbmuxd.h sock_stuff.h
sock_stuff.o: sock_stuff.c sock_stuff.h
libusbmuxd.o: libusbmuxd.c libusbmuxd.h usbmuxd.h
iproxy.o: iproxy.c sock_stuff.h

%.o:    %.c
	$(CC) -o $@ $(CFLAGS) -c $< 

usbmuxd: main.o sock_stuff.o iphone.o
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

iproxy: iproxy.o libusbmuxd.o sock_stuff.o
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

clean:
	rm -f *.o $(TARGETS)

realclean: clean
	rm -f *~

install: all
	cp usbmuxd $(INSTALL_PREFIX)/sbin/
	cp usbmuxd.h $(INSTALL_PREFIX)/include/

.PHONY: all clean realclean
