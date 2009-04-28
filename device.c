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

#include <stdlib.h>
#include "device.h"
#include "usb.h"
#include "log.h"

int device_id;
/*
int get_next_device_id(void)
{
	int i;
	while(1) {
		for(i=0; i<num_devs; i++) {
			if(device_list[i].dev && device_list[i].id == device_id) {
				device_id++;
				break;
			}
		}
		if(i < num_devs)
			break;
	}
	return device_id++;
}
*/
void device_add(struct usb_device *dev)
{
	usbmuxd_log(LL_NOTICE, "Connected to new device on location 0x%x with serial number %s", usb_get_location(dev), usb_get_serial(dev));
}

void device_remove(struct usb_device *dev)
{
	usbmuxd_log(LL_NOTICE, "Removed device on location 0x%x with serial number %s", usb_get_location(dev), usb_get_serial(dev));
}
