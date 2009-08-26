#ifndef __USBMUXD_H
#define __USBMUXD_H

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
enum usbmuxd_device_event {
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
 * @param available_devices pointer to an array of usbmuxd_device_info_t
 *      that will hold records of the connected devices.
 *
 * @return number of available devices, zero on no devices, or negative on error
 */
int usbmuxd_scan(usbmuxd_device_info_t **available_devices);

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
