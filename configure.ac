# Inital configuration

AC_PREREQ(2.61)

AC_INIT([usbmuxd], [0.1.0], [nikias@gmx.li])
AM_INIT_AUTOMAKE([-Wall -Werror foreign dist-bzip2])

AC_CONFIG_HEADERS([config.h])

# Check for programs

AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_LIBTOOL
AC_PROG_INSTALL

# Check for libraries

PKG_CHECK_MODULES(libusb, libusb >= 0.1.12)

# check for options
AC_ARG_ENABLE([hacks],
            [AS_HELP_STRING([--disable-hacks],
            [disable hacks that provide workarounds for certain problems])],
            [enable_hacks=no],
            [enable_hacks=yes])
if test "x$enable_hacks" = xyes; then
        AC_DEFINE(ENABLE_HACKS, 1, [Define if you want hacks enabled])
        echo "Note: hacks are enabled."
else
        echo "Note: hacks are DISABLED."
fi
AM_CONDITIONAL(ENABLE_HACKS, test "x$enable_hacks" = xyes)

# Output files

AC_OUTPUT([
Makefile
src/Makefile
tools/Makefile
udev/85-usbmuxd.rules
udev/Makefile
libusbmuxd.pc
])
