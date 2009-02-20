TARGET=usbmuxd
CFLAGS=-Wall
LDFLAGS=-lpthread -lusb -lrt

objects = sock_stuff.o usbmuxd.o iphone.o

all:	$(TARGET)

%.o:    %.c %.h
	$(CC) -o $@ $(CFLAGS) -c $< 

$(TARGET): $(objects)
	$(CC) -o $@ $(LDFLAGS) $^

clean:
	rm -f *.o $(TARGET)

realclean: clean
	rm -f *~

testclient: testclient.c sock_stuff.o
	$(CC) $(LDFLAGS) -o testclient $(CFLAGS) $< sock_stuff.o

iproxy: iproxy.c sock_stuff.o
	$(CC) -lpthread -o iproxy $(CFLAGS) $< sock_stuff.o

