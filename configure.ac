# Inital configuration

AC_PREREQ(2.61)

AC_INIT([usbmuxd], [0.1.5], [nikias@gmx.li])
AM_INIT_AUTOMAKE([-Wall -Werror foreign dist-bzip2])

AC_CONFIG_HEADERS([config.h])

# Check for programs

AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_LIBTOOL
AC_PROG_INSTALL

# Check for libraries

PKG_CHECK_MODULES(libusb, libusb >= 0.1.12)

# Output files

AC_OUTPUT([
Makefile
src/Makefile
tools/Makefile
udev/85-usbmuxd.rules
udev/Makefile
libusbmuxd.pc
])
