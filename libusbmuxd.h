#ifndef __LIBUSBMUXD_H
#define __LIBUSBMUXD_H

#include <usbmuxd.h>

/**
 * Contacts usbmuxd via it's unix domain socket and performs a scan for
 *  connected devices.
 *
 * @param devices Pointer to an array of usbmuxd_device_t.
 * 	 Assumed initially NULL, will be allocated by this function.
 *
 * @return number of devices found, negative on error
 */
int usbmuxd_scan(usbmuxd_device_t **devices);

/**
 * Performs the connect procedure via usbmuxd.
 *
 * @param device_id USB device number of the device to connect to
 * @param port Port number to connect to
 *
 * @return socket of the connection, negative on error
 */
int usbmuxd_connect(uint32_t device_id, uint16_t port);

#endif /* __LIBUSBMUXD_H */
