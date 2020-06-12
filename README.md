# usbmuxd

*A socket daemon to multiplex connections from and to iOS devices.*

## Features

usbmuxd stands for "USB multiplexing daemon". This daemon is in charge of
multiplexing connections over USB to an iOS device.

To users, it means you can sync your music, contacts, photos, etc. over USB.

To developers, it means you can connect to any listening localhost socket on the
device.

usbmuxd is not used for tethering data transfer which uses a dedicated USB
interface as a virtual network device.

Multiple connections to different TCP ports can happen in parallel.

The higher-level layers are handled by [libimobiledevice](https://github.com/libimobiledevice/libimobiledevice.git).
The low-level layer is handled by [libusbmuxd](https://github.com/libimobiledevice/libusbmuxd.git).

## Installation / Getting started

### Debian / Ubuntu Linux

First install all required dependencies and build tools:
```shell
sudo apt-get install \
	build-essential \
	checkinstall \
	git \
	autoconf \
	automake \
	libtool-bin \
	libplist-dev \
	libusbmuxd-dev \
	libimobiledevice-dev \
	libusb-1.0-0-dev \
	udev
```

If systemd is not installed and should control spawning the daemon use:
```shell
sudo apt-get install \
	systemd
```

Then clone the actual project repository:
```shell
git clone https://github.com/libimobiledevice/usbmuxd.git
cd usbmuxd
```

Now you can build and install it:
```shell
./autogen.sh
make
sudo make install
```

If you require a custom prefix or other option being passed to `./configure`
you can pass them directly to `./autogen.sh` like this:
```bash
./autogen.sh --prefix=/opt/local --without-preflight --without-systemd
make
sudo make install
```

To output a list of available configure options use:
```bash
./autogen.sh --help
```

## Usage

The daemon is automatically started by udev or systemd depending on what you
have configured upon hotplug of an iOS device and exits if the last device
was unplugged.

When usbmuxd is running it provides a socket interface at `/var/run/usbmuxd`
that is designed to be compatible with the socket interface that is provided
on macOS.

You should also create an `usbmux` user that has access to USB devices on your
system. Alternatively, just pass a different username using the `-U` argument.

The daemon also manages pairing records with iOS devices and the host in
`/var/lib/lockdown` (Linux) or `/var/db/lockdown` (macOS).

Ensure proper permissions are setup for the daemon to access the directory.

For debugging purposes it is helpful to start usbmuxd using the foreground `-f`
argument and enable verbose mode `-v` to get suitable logs.

Please consult the usage information or manual page for a full documentation of
available command line options:
```shell
usbmuxd --help
man usbmuxd
```

## Links

* Homepage: https://libimobiledevice.org/
* Repository: https://git.libimobiledevice.org/usbmuxd.git
* Repository (Mirror): https://github.com/libimobiledevice/usbmuxd.git
* Issue Tracker: https://github.com/libimobiledevice/usbmuxd/issues
* Mailing List: https://lists.libimobiledevice.org/mailman/listinfo/libimobiledevice-devel
* Twitter: https://twitter.com/libimobiledev

## License

This library and utilities are licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.en.html),
also included in the repository in the `COPYING.GPLv3` file.

## Credits

The initial usbmuxd daemon implementation was authored by Hector Martin.

Apple, iPhone, iPod, and iPod Touch are trademarks of Apple Inc.

usbmuxd is an independent software application and has not been
authorized, sponsored, or otherwise approved by Apple Inc.

README Updated on: 2020-06-13
