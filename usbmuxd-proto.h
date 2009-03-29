/* Protocol defintion for usbmuxd proxy protocol */

#ifndef __USBMUXD_H
#define __USBMUXD_H

#include <stdint.h>

#define USBMUXD_SOCKET_FILE "/var/run/usbmuxd"

struct usbmuxd_header {
	uint32_t length;    // length of message, including header
	uint32_t reserved;  // always zero
	uint32_t type;      // message type
	uint32_t tag;       // responses to this query will echo back this tag
} __attribute__((__packed__));

struct usbmuxd_result {
	struct usbmuxd_header header;
	uint32_t result;
} __attribute__((__packed__));

struct	usbmuxd_connect_request {
	struct usbmuxd_header header;
	uint32_t device_id;
	uint16_t tcp_dport;   // TCP port number
	uint16_t reserved;   // set to zero
} __attribute__((__packed__));

struct usbmuxd_device {
	uint32_t device_id;
	uint16_t product_id;
	char serial_number[40];
} __attribute__((__packed__));

struct usbmuxd_device_info_record {
	struct usbmuxd_header header;
	struct usbmuxd_device device;
	char padding[222];
} __attribute__((__packed__));

struct usbmuxd_scan_request {
	struct usbmuxd_header header;
} __attribute__((__packed__));

enum {
	USBMUXD_RESULT  = 1,
	USBMUXD_CONNECT = 2,
	USBMUXD_SCAN = 3,
	USBMUXD_DEVICE_INFO = 4,
};

#endif /* __USBMUXD_PROTO_H */
