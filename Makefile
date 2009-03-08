TARGETS=usbmuxd iproxy testclient
CFLAGS=-Wall -g -DDEBUG
LIBS=-lpthread -lusb -lrt
LDFLAGS=

all:	$(TARGETS)

main.o: main.c usbmuxd.h sock_stuff.h iphone.h
iphone.o: iproxy.c iphone.h usbmuxd.h sock_stuff.h
sock_stuff.o: sock_stuff.c sock_stuff.h
testclient.o: testclient.c sock_stuff.h

%.o:    %.c
	$(CC) -o $@ $(CFLAGS) -c $< 

usbmuxd: main.o sock_stuff.o iphone.o
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

testclient: testclient.o sock_stuff.o
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

iproxy: iproxy.o sock_stuff.o
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

clean:
	rm -f *.o $(TARGETS)

realclean: clean
	rm -f *~

.PHONY: all clean realclean
