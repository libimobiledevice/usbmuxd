/* Protocol defintion for usbmuxd proxy protocol */
#ifndef __USBMUXD_PROTO_H
#define __USBMUXD_PROTO_H

#include <stdint.h>
#define USBMUXD_PROTOCOL_VERSION 0

#define USBMUXD_SOCKET_FILE "/var/run/usbmuxd"

enum usbmuxd_result {
	RESULT_OK = 0,
	RESULT_BADCOMMAND = 1,
	RESULT_BADDEV = 2,
	RESULT_CONNREFUSED = 3,
	// ???
	// ???
	RESULT_BADVERSION = 6,
};

enum usbmuxd_msgtype {
	MESSAGE_RESULT  = 1,
	MESSAGE_CONNECT = 2,
	MESSAGE_LISTEN = 3,
	MESSAGE_DEVICE_ADD = 4,
	MESSAGE_DEVICE_REMOVE = 5,
	//???
	//???
	//MESSAGE_PLIST = 8,
};

struct usbmuxd_header {
	uint32_t length;    // length of message, including header
	uint32_t version;   // protocol version
	uint32_t message;   // message type
	uint32_t tag;       // responses to this query will echo back this tag
} __attribute__((__packed__));

struct usbmuxd_result_msg {
	struct usbmuxd_header header;
	uint32_t result;
} __attribute__((__packed__));

struct usbmuxd_connect_request {
	struct usbmuxd_header header;
	uint32_t device_id;
	uint16_t port;   // TCP port number
	uint16_t reserved;   // set to zero
} __attribute__((__packed__));

struct usbmuxd_listen_request {
	struct usbmuxd_header header;
} __attribute__((__packed__));

struct usbmuxd_device_record {
	uint32_t device_id;
	uint16_t product_id;
	char serial_number[256];
	uint16_t padding;
	uint32_t location;
} __attribute__((__packed__));

#endif /* __USBMUXD_PROTO_H */
