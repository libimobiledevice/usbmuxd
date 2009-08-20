#ifndef __USBMUXD_H
#define __USBMUXD_H

/**
 * Array entry returned by 'usbmuxd_scan()' scanning.
 *
 * If more than one device is available, 'product_id' and
 * 'serial_number' and be analysed to help make a selection.
 * The relevant 'handle' should be passed to 'usbmuxd_connect()', to
 * start a proxy connection.  The value 'handle' should be considered
 * opaque and no presumption made about the meaning of its value.
 */
typedef struct {
	int handle;
	int product_id;
	char serial_number[41];
} usbmuxd_scan_result;

/**
 * Contacts usbmuxd and performs a scan for connected devices.
 *
 * @param available_devices pointer to array of usbmuxd_scan_result.
 * 	Array of available devices.  The required 'handle'
 *	should be passed to 'usbmuxd_connect()'.  The returned array
 *	is zero-terminated for convenience; the final (unused)
 *	entry containing handle == 0.  The returned array pointer
 *	should be freed by passing to 'free()' after use.
 *
 * @return number of available devices, zero on no devices, or negative on error
 */
int usbmuxd_scan(usbmuxd_scan_result **available_devices);

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

#endif /* __USBMUXD_H */
