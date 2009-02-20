#ifndef __USBMUXD_H
#define __USBMUXD_H

#include <stdint.h>

struct usbmux_header {
	uint32_t length;    // length of message, including header
	uint32_t reserved;  // always zero
	uint32_t type;      // message type
	uint32_t tag;       // responses to this query will echo back this tag
};

struct usbmux_result {
	struct usbmux_header header;
	uint32_t result;
};

struct	usbmux_connect_request {
	struct usbmux_header header;
	uint32_t device_id;
	uint16_t port;	     // TCP port number
	uint16_t reserved;   // set to zero
};

struct usbmux_dev_info {
	uint32_t device_id;
	uint16_t product_id;
	char serial_number[40];
};

struct usbmux_dev_info_request {
	struct usbmux_header header;
	struct usbmux_dev_info dev_info;
	unsigned char padding[222];
};

enum {
	usbmux_result  = 1,
	usbmux_connect = 2,
	usbmux_hello   = 3,
	usbmux_device_info = 4,
};

#endif
