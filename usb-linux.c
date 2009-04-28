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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <libusb.h>

#include "usb.h"
#include "log.h"
#include "device.h"

// interval for device connection/disconnection polling, in milliseconds
// we need this because there is currently no asynchronous device discovery mechanism in libusb
#define DEVICE_POLL_TIME 1000

struct usb_device {
	libusb_device_handle *dev;
	uint8_t bus, address;
	char serial[256];
	int alive;
};

int num_devs;
int device_id;
struct usb_device *device_list;

struct timeval next_dev_poll_time;

static int alloc_device(void)
{
	int i;
	for(i=0; i<num_devs; i++) {
		if(!device_list[i].dev)
			return i;
	}
	num_devs++;
	device_list = realloc(device_list, sizeof(*device_list) * num_devs);
	memset(&device_list[num_devs-1], 0, sizeof(*device_list));
	return num_devs - 1;
}

static void usb_disconnect(struct usb_device *dev)
{
	if(!dev->dev) {
		return;
	}
	libusb_release_interface(dev->dev, USB_INTERFACE);
	libusb_close(dev->dev);
	dev->dev = NULL;
}

static int usb_discover(void)
{
	int cnt, i, j, res;
	int valid_count = 0;
	libusb_device **devs;
	
	cnt = libusb_get_device_list(NULL, &devs);
	if(cnt < 0) {
		usbmuxd_log(LL_FATAL, "Could not get device list: %d", cnt);
		return cnt;
	}

	usbmuxd_log(LL_SPEW, "usb_discover: scanning %d devices", cnt);

	for(j=0; j<num_devs; j++) {
		device_list[j].alive = 0;
	}
	for(i=0; i<cnt; i++) {
		// the following are non-blocking operations on the device list
		libusb_device *dev = devs[i];
		uint8_t bus = libusb_get_bus_number(dev);
		uint8_t address = libusb_get_device_address(dev);
		struct libusb_device_descriptor devdesc;
		for(j=0; j<num_devs; j++) {
			if(device_list[j].dev && device_list[j].bus == bus && device_list[j].address == address) {
				valid_count++;
				device_list[j].alive = 1;
				break;
			}
		}
		if(j < num_devs)
			continue; //device already found
		if((res = libusb_get_device_descriptor(dev, &devdesc)) != 0) {
			usbmuxd_log(LL_WARNING, "Could not get device descriptor for device %d-%d: %d", bus, address, res);
			continue;
		}
		if(devdesc.idVendor != VID_APPLE)
			continue;
		if( (devdesc.idProduct != PID_IPHONE2G) &&
			(devdesc.idProduct != PID_ITOUCH1G) &&
			(devdesc.idProduct != PID_IPHONE3G))
			continue;
		libusb_device_handle *handle;
		usbmuxd_log(LL_INFO, "Found new device with v/p %04x:%04x at %d-%d", devdesc.idVendor, devdesc.idProduct, bus, address);
		// potentially blocking operations follow; they will only run when new devices are detected, which is acceptable
		if((res = libusb_open(dev, &handle)) != 0) {
			usbmuxd_log(LL_WARNING, "Could not open device %d-%d: %d", bus, address, res);
			continue;
		}
		if((res = libusb_set_configuration(handle, USB_CONFIGURATION)) != 0) {
			usbmuxd_log(LL_WARNING, "Could not set configuration %d for device %d-%d: %d", USB_CONFIGURATION, bus, address, res);
			libusb_close(handle);
			continue;
		}
		if((res = libusb_claim_interface(handle, USB_INTERFACE)) != 0) {
			usbmuxd_log(LL_WARNING, "Could not claim interface %d for device %d-%d: %d", USB_INTERFACE, bus, address, res);
			libusb_close(handle);
			continue;
		}
		int idx = alloc_device();

		if((res = libusb_get_string_descriptor_ascii(handle, devdesc.iSerialNumber, (uint8_t *)device_list[idx].serial, 256)) <= 0) {
			usbmuxd_log(LL_WARNING, "Could not get serial number for device %d-%d: %d", USB_INTERFACE, bus, address, res);
			libusb_close(handle);
			continue;
		}
		device_list[idx].serial[res] = 0;
		device_list[idx].bus = bus;
		device_list[idx].address = address;
		device_list[idx].dev = handle;
		device_list[idx].alive = 1;
		
		device_add(&device_list[idx]);
		valid_count++;
	}
	for(j=0; j<num_devs; j++) {
		if(device_list[j].dev && !device_list[j].alive) {
			device_remove(&device_list[j]);
			usb_disconnect(&device_list[j]);
		}
	}
	libusb_free_device_list(devs, 1);
	
	gettimeofday(&next_dev_poll_time, NULL);
	next_dev_poll_time.tv_usec += DEVICE_POLL_TIME * 1000;
	next_dev_poll_time.tv_sec += next_dev_poll_time.tv_usec / 1000000;
	next_dev_poll_time.tv_usec = next_dev_poll_time.tv_usec % 1000000;
	
	return valid_count;
}

const char *usb_get_serial(struct usb_device *dev)
{
	if(!dev->dev) {
		return NULL;
	}
	return dev->serial;
}

int usb_get_location(struct usb_device *dev)
{
	if(!dev->dev) {
		return 0;
	}
	return (dev->bus << 16) | dev->address;
}

void usb_get_fds(struct fdlist *list)
{
	const struct libusb_pollfd **usbfds;
	const struct libusb_pollfd **p;
	usbfds = libusb_get_pollfds(NULL);
	if(!usbfds) {
		usbmuxd_log(LL_ERROR, "libusb_get_pollfds failed");
		return;
	}
	p = usbfds;
	while(*p) {
		fdlist_add(list, FD_USB, (*p)->fd, (*p)->events);
		p++;
	}
	free(usbfds);
}

static int dev_poll_remain_ms(void)
{
	int msecs;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	msecs = (next_dev_poll_time.tv_sec - tv.tv_sec) * 1000;
	msecs += (next_dev_poll_time.tv_usec - tv.tv_usec) / 1000;
	if(msecs < 0)
		return 0;
	return msecs;
}

int usb_get_timeout(void)
{
	struct timeval tv;
	int msec;
	int res;
	int pollrem;
	pollrem = dev_poll_remain_ms();
	res = libusb_get_next_timeout(NULL, &tv);
	if(res == 0)
		return pollrem;
	if(res < 0) {
		usbmuxd_log(LL_ERROR, "libusb_get_next_timeout failed: %d", res);
		return pollrem;
	}
	msec = tv.tv_sec * 1000;
	msec += tv.tv_usec / 1000;
	if(msec > pollrem)
		return pollrem;
	return msec;
}

int usb_process(void)
{
	int res;
	struct timeval tv;
	tv.tv_sec = tv.tv_usec = 0;
	res = libusb_handle_events_timeout(NULL, &tv);
	if(res < 0) {
		usbmuxd_log(LL_ERROR, "libusb_handle_events_timeout failed: %d", res);
		return res;
	}
	if(dev_poll_remain_ms() <= 0) {
		res = usb_discover();
		if(res < 0) {
			usbmuxd_log(LL_ERROR, "usb_discover failed: %d", res);
			return res;
		}
	}
	return 0;
}

int usb_init(void)
{
	int res;
	usbmuxd_log(LL_DEBUG, "usb_init for linux / libusb 1.0");
	
	res = libusb_init(NULL);
	if(res != 0) {
		usbmuxd_log(LL_FATAL, "libusb_init failed: %d", res);
		return -1;
	}
	
	device_id = 1;
	num_devs = 1;
	device_list = malloc(sizeof(*device_list) * num_devs);
	memset(device_list, 0, sizeof(*device_list) * num_devs);
	
	return usb_discover();
}

void usb_shutdown(void)
{
	int i;
	usbmuxd_log(LL_DEBUG, "usb_shutdown");
	for(i=0; i<num_devs; i++)
		usb_disconnect(&device_list[i]);
	free(device_list);
	device_list = NULL;
	libusb_exit(NULL);
}
