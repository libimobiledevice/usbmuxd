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

#ifndef __USB_H__
#define __USB_H__

#include <stdint.h>
#include "utils.h"

#define BULK_IN 0x85
#define BULK_OUT 0x04

// libusb fragments packets larger than this (usbfs limitation)
// on input, this creates race conditions and other issues
// I don't think the device uses larger packets
// if it does then we're going to have to implement proper framing...
#define USB_MRU 16384

// max transmission packet size
// libusb fragments these too, but doesn't send ZLPs so we're safe
// but maybe we need to send a ZLP ourselves at the end (see usb-linux.h)
// we're using 3 * 16384 to optimize for the fragmentation
// this results in three URBs per full transfer, 32 USB packets each
// if there are ZLP issues this should make them show up too
#define USB_MTU (3 * 16384)

#define USB_PACKET_SIZE 512

#define VID_APPLE 0x5ac
#define PID_IPHONE2G 0x1290
#define PID_ITOUCH1G 0x1291
#define PID_IPHONE3G 0x1292

#define USB_CONFIGURATION 3
#define USB_INTERFACE 1

struct usb_device;

int usb_init(void);
void usb_shutdown(void);
const char *usb_get_serial(struct usb_device *dev);
uint32_t usb_get_location(struct usb_device *dev);
uint16_t usb_get_pid(struct usb_device *dev);
void usb_get_fds(struct fdlist *list);
int usb_get_timeout(void);
int usb_send(struct usb_device *dev, const unsigned char *buf, int length);
int usb_process(void);
int usb_process_timeout(int msec);

#endif
