/*
	usbmuxd - iPhone/iPod Touch USB multiplex server daemon

Copyright (C) 2009	Hector Martin "marcan" <hector@marcansoft.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 or version 3.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#define _BSD_SOURCE

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include "device.h"
#include "usb.h"
#include "log.h"

int next_device_id;

enum mux_protocol {
	MUX_PROTO_VERSION = 0,
	MUX_PROTO_TCP = IPPROTO_TCP,
};

enum mux_dev_state {
	MUXDEV_INIT,
	MUXDEV_ACTIVE,
	MUXDEV_DEAD
};

struct mux_header
{
	uint32_t protocol;
	uint32_t length;
};

struct version_header
{
	uint32_t major;
	uint32_t minor;
	uint32_t padding;
};

struct mux_device
{
	struct usb_device *usbdev;
	int id;
	enum mux_dev_state state;
};

static int num_devs;
static struct mux_device *device_list;

static int alloc_device(void)
{
	int i;
	for(i=0; i<num_devs; i++) {
		if(!device_list[i].usbdev)
			return i;
	}
	num_devs++;
	device_list = realloc(device_list, sizeof(*device_list) * num_devs);
	memset(&device_list[num_devs-1], 0, sizeof(*device_list));
	return num_devs - 1;
}

static int get_next_device_id(void)
{
	int i;
	while(1) {
		for(i=0; i<num_devs; i++) {
			if(device_list[i].usbdev && device_list[i].id == next_device_id) {
				next_device_id++;
				break;
			}
		}
		if(i >= num_devs)
			return next_device_id++;
	}
}

int send_packet(struct mux_device *dev, enum mux_protocol proto, void *header, void *data, int length)
{
	unsigned char *buffer;
	int hdrlen;
	int res;
	
	switch(proto) {
		case MUX_PROTO_VERSION:
			hdrlen = sizeof(struct version_header);
			break;
		case MUX_PROTO_TCP:
			hdrlen = sizeof(struct tcphdr);
			break;
		default:
			usbmuxd_log(LL_ERROR, "Invalid protocol %d for outgoing packet (dev %d hdr %p data %p len %d)", proto, dev->id, header, data, length);
			return -1;
	}
	usbmuxd_log(LL_SPEW, "send_packet(%d, 0x%x, %p, %p, %d)", dev->id, proto, header, data, length);
	
	int total = sizeof(struct mux_header) + hdrlen + length;
	
	if(total > USB_MTU) {
		usbmuxd_log(LL_ERROR, "Tried to send packet larger than USB MTU (hdr %d data %d total %d) to device %d", hdrlen, length, total, dev->id);
		return -1;
	}
	
	buffer = malloc(total);
	struct mux_header *mhdr = (struct mux_header *)buffer;
	mhdr->protocol = htonl(proto);
	mhdr->length = htonl(total);;
	memcpy(buffer + sizeof(struct mux_header), header, hdrlen);
	if(data && length)
		memcpy(buffer + sizeof(struct mux_header) + hdrlen, data, length);
	
	if((res = usb_send(dev->usbdev, buffer, total)) < 0) {
		usbmuxd_log(LL_ERROR, "usb_send failed while sending packet (len %d) to device %d: %d", total, dev->id, res);
		free(buffer);
		return res;
	}
	return mhdr->length;
}

static void device_version_input(struct mux_device *dev, struct version_header *vh)
{
	if(dev->state != MUXDEV_INIT) {
		usbmuxd_log(LL_WARNING, "Version packet from already initialized device %d", dev->id);
		return;
	}
	vh->major = ntohl(vh->major);
	vh->minor = ntohl(vh->minor);
	if(vh->major != 1 || vh->minor != 0) {
		usbmuxd_log(LL_ERROR, "Device %d has unknown version %d.%d\n", dev->id, vh->major, vh->minor);
		return;
	}
	usbmuxd_log(LL_NOTICE, "Connected to v%d.%d device %d on location 0x%x with serial number %s", vh->major, vh->minor, dev->id, usb_get_location(dev->usbdev), usb_get_serial(dev->usbdev));
}

static void device_tcp_input(struct mux_device *dev, struct tcphdr *th, unsigned char *payload, int payload_length)
{

}


void device_data_input(struct usb_device *usbdev, unsigned char *buffer, int length)
{
	int i;
	struct mux_device *dev;
	for(i=0; i<num_devs; i++) {
		if(device_list[i].usbdev == usbdev) {
			dev = &device_list[i];
			break;
		}
	}
	if(i >= num_devs) {
		usbmuxd_log(LL_WARNING, "Cannot find device entry for RX input from USB device %p on location 0x%x", usbdev, usb_get_location(usbdev));
		return;
	}
	
	usbmuxd_log(LL_SPEW, "Mux data input for device %p: %p len %d", dev, buffer, length);
	
	struct mux_header *mhdr = (struct mux_header *)buffer;
	
	if(ntohl(mhdr->length) != length) {
		usbmuxd_log(LL_ERROR, "Incoming packet size mismatch (dev %d, expected %d, got %d)", dev->id, ntohl(mhdr->length), length);
		return;
	}
	
	struct tcphdr *th;
	unsigned char *payload;
	int payload_length;
	
	switch(ntohl(mhdr->protocol)) {
		case MUX_PROTO_VERSION:
			device_version_input(dev, (struct version_header *)(mhdr+1));
			break;
		case MUX_PROTO_TCP:
			th = (struct tcphdr *)(mhdr+1);
			payload = (unsigned char *)(th+1);
			payload_length = length - sizeof(struct tcphdr) - sizeof(struct mux_header);
			device_tcp_input(dev, (struct tcphdr *)(mhdr+1), payload, payload_length);
			break;
		default:
			usbmuxd_log(LL_ERROR, "Incoming packet for device %d has unknown protocol 0x%x)", dev->id, ntohl(mhdr->protocol));
			break;
	}
	
}

int device_add(struct usb_device *dev)
{
	int res;
	int id = get_next_device_id();
	int idx = alloc_device();
	usbmuxd_log(LL_NOTICE, "Connecting to new device on location 0x%x as ID %d", usb_get_location(dev), id);
	device_list[idx].id = id;
	device_list[idx].usbdev = dev;
	device_list[idx].state = MUXDEV_INIT;
	struct version_header vh;
	vh.major = htonl(1);
	vh.minor = htonl(0);
	vh.padding = 0;
	if((res = send_packet(&device_list[idx], MUX_PROTO_VERSION, &vh, NULL, 0)) < 0) {
		usbmuxd_log(LL_ERROR, "Error sending version request packet to device %d\n", id);
		device_list[idx].usbdev = NULL;
		device_list[idx].state = MUXDEV_DEAD;
		return res;
	}
	return 0;
}

void device_remove(struct usb_device *dev)
{
	int i;
	for(i=0; i<num_devs; i++) {
		if(device_list[i].usbdev == dev) {
			usbmuxd_log(LL_NOTICE, "Removed device %d on location 0x%x", device_list[i].id, usb_get_location(dev));
			device_list[i].usbdev = NULL;
			return;
		}
	}
	usbmuxd_log(LL_WARNING, "Cannot find device entry while removing USB device %p on location 0x%x", dev, usb_get_location(dev));
}

void device_init(void)
{
	usbmuxd_log(LL_DEBUG, "device_init");
	num_devs = 1;
	device_list = malloc(sizeof(*device_list) * num_devs);
	memset(device_list, 0, sizeof(*device_list) * num_devs);
	next_device_id = 1;
}

void device_shutdown(void)
{
	int i;
	usbmuxd_log(LL_DEBUG, "device_shutdown");
	for(i=0; i<num_devs; i++)
		device_remove(device_list[i].usbdev);
	free(device_list);
	device_list = NULL;
}
