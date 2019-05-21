# usbmuxd

## About

A socket daemon to multiplex connections from and to iOS devices.

## Background

usbmuxd stands for "USB multiplexing daemon". This daemon is in charge of
multiplexing connections over USB to an iOS device. To users, it means
you can sync your music, contacts, photos, etc. over USB. To developers, it
means you can connect to any listening localhost socket on the device. usbmuxd
is not used for tethering data transfer which uses a dedicated USB interface as
a virtual network device. Multiple connections to different TCP ports can happen
in parallel. The higher-level layers are handled by libimobiledevice.

When usbmuxd is running (normally started or stopped as a result of _udev_
auto-insertion messages, or by _systemd_) it provides a socket interface in
`/var/run/usbmuxd` that is designed to be compatible with the socket interface
that is provided on macOS.

You should also create a `usbmux` user that has access to USB devices on your
system. Alternatively, you can pass a different username using the `-U` argument.

The daemon also manages pairing records with iOS devices and the host in
`/var/lib/lockdown` (Linux) or `/var/db/lockdown` (macOS).
Ensure proper permissions are setup for the daemon to access the directory.

## Requirements

Development Packages of:
* libimobiledevice
* libplist
* libusb

Software:
* make
* autoheader
* automake
* autoconf
* libtool
* pkg-config
* gcc or clang
* udev (Linux only)

Optional:
* systemd (Linux only)

## Installation

To compile run:

```bash
./autogen.sh
make
sudo make install
```

The daemon is automatically started by udev or systemd depending on what you
have configured it on hotplug of an iOS device and exits if the last device
was unplugged.

For debugging purposes it is helpful to start usbmuxd using the foreground `-f`
argument and enable verbose mode `-v` to get suitable logs.

## Who/What/Where?

* Home: https://www.libimobiledevice.org/
* Code: `git clone https://git.libimobiledevice.org/usbmuxd.git`
* Code (Mirror): `git clone https://github.com/libimobiledevice/usbmuxd.git`
* Tickets: https://github.com/libimobiledevice/usbmuxd/issues
* Mailing List: https://lists.libimobiledevice.org/mailman/listinfo/libimobiledevice-devel
* IRC: irc://irc.freenode.net#libimobiledevice
* Twitter: https://twitter.com/libimobiledev

## Credits

The first usbmuxd daemon implementation was authored by Hector Martin.

Apple, iPhone, iPod, and iPod Touch are trademarks of Apple Inc.
libimobiledevice is an independent software library and has not been
authorized, sponsored, or otherwise approved by Apple Inc.

README Updated on: 2019-05-16
