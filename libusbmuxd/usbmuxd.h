/*
	libusbmuxd - client library to talk to usbmuxd

Copyright (C) 2009	Nikias Bassen <nikias@gmx.li>
Copyright (C) 2009	Paul Sladen <libiphone@paul.sladen.org>
Copyright (C) 2009	Martin Szulecki <opensuse@sukimashita.com>

This library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation, either version 2.1 of the
License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#ifndef __USBMUXD_H
#define __USBMUXD_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Device information structure holding data to identify the device.
 * The relevant 'handle' should be passed to 'usbmuxd_connect()', to
 * start a proxy connection.  The value 'handle' should be considered
 * opaque and no presumption made about the meaning of its value.
 */
typedef struct {
	int handle;
	int product_id;
	char uuid[41];
} usbmuxd_device_info_t;

/**
 * event types for event callback function
 */
enum usbmuxd_event_type {
    UE_DEVICE_ADD = 1,
    UE_DEVICE_REMOVE
};

/**
 * Event structure that will be passed to the callback function.
 * 'event' will contains the type of the event, and 'device' will contains
 * information about the device.
 */
typedef struct {
    int event;
    usbmuxd_device_info_t device;
} usbmuxd_event_t;

/**
 * Callback function prototype.
 */
typedef void (*usbmuxd_event_cb_t) (const usbmuxd_event_t *event, void *user_data);

/**
 * Subscribe a callback function so that applications get to know about
 * device add/remove events.
 *
 * @param callback A callback function that is executed when an event occurs.
 *
 * @return 0 on success or negative on error.
 */
int usbmuxd_subscribe(usbmuxd_event_cb_t callback, void *user_data);

/**
 * Unsubscribe callback.
 *
 * @return only 0 for now.
 */
int usbmuxd_unsubscribe();

/**
 * Contacts usbmuxd and retrieves a list of connected devices.
 *
 * @param device_list A pointer to an array of usbmuxd_device_info_t
 *      that will hold records of the connected devices. The last record
 *      is a null-terminated record with all fields set to 0/NULL.
 * @note The user has to free the list returned.
 *
 * @return number of attached devices, zero on no devices, or negative
 *   if an error occured.
 */
int usbmuxd_get_device_list(usbmuxd_device_info_t **device_list);

/**
 * Frees the device list returned by an usbmuxd_get_device_list call
 *
 * @param device_list A pointer to an array of usbmuxd_device_info_t to free.
 *
 * @return 0 on success, -1 on error.
 */
int usbmuxd_device_list_free(usbmuxd_device_info_t **device_list);

/**
 * Gets device information for the device specified by uuid.
 *
 * @param uuid A device uuid of the device to look for. If uuid is NULL,
 *      This function will return the first device found.
 * @param device Pointer to a previously allocated (or static) 
 *      usbmuxd_device_info_t that will be filled with the device info.
 *
 * @return 0 if no matching device is connected, 1 if the device was found,
 *    or a negative value on error.
 */
int usbmuxd_get_device_by_uuid(const char *uuid, usbmuxd_device_info_t *device);

/**
 * Request proxy connect to 
 *
 * @param handle returned by 'usbmuxd_scan()'
 *
 * @param tcp_port TCP port number on device, in range 0-65535.
 *	common values are 62078 for lockdown, and 22 for SSH.
 *
 * @return file descriptor socket of the connection, or -1 on error
 */
int usbmuxd_connect(const int handle, const unsigned short tcp_port);

/**
 * Disconnect. For now, this just closes the socket file descriptor.
 *
 * @param sfd socker file descriptor returned by usbmuxd_connect()
 *
 * @return 0 on success, -1 on error.
 */
int usbmuxd_disconnect(int sfd);

/**
 * Send data to the specified socket.
 *
 * @param sfd socket file descriptor returned by usbmuxd_connect()
 * @param data buffer to send
 * @param len size of buffer to send
 * @param sent_bytes how many bytes sent
 *
 * @return 0 on success, a negative errno value otherwise.
 */
int usbmuxd_send(int sfd, const char *data, uint32_t len, uint32_t *sent_bytes);

/**
 * Receive data from the specified socket.
 *
 * @param sfd socket file descriptor returned by usbmuxd_connect()
 * @param data buffer to put the data to
 * @param len number of bytes to receive
 * @param recv_bytes number of bytes received
 * @param timeout how many milliseconds to wait for data
 *
 * @return 0 on success, a negative errno value otherwise.
 */
int usbmuxd_recv_timeout(int sfd, char *data, uint32_t len, uint32_t *recv_bytes, unsigned int timeout);

/**
 * Receive data from the specified socket with a default timeout.
 *
 * @param sfd socket file descriptor returned by usbmuxd_connect()
 * @param data buffer to put the data to
 * @param len number of bytes to receive
 * @param recv_bytes number of bytes received
 *
 * @return 0 on success, a negative errno value otherwise.
 */
int usbmuxd_recv(int sfd, char *data, uint32_t len, uint32_t *recv_bytes);

#ifdef __cplusplus
}
#endif

#endif /* __USBMUXD_H */
