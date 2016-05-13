#ifdef WIN32
#include "usb_win32.h"
#include <stdint.h>
#include <lusb0_usb.h>
#include "log.h"
#include "usb.h"

usb_dev_handle *usb_win32_open(const char serial[]);

void usb_win32_init()
{
	usb_init();

#if _DEBUG
	usb_set_debug(255);
#endif

	usb_find_busses(); /* find all busses */
	usb_find_devices(); /* find all connected devices */
}

void usb_win32_set_configuration(const char serial[], uint8_t configuration)
{
	usbmuxd_log(LL_INFO, "Setting configuration for device %s using libusb-win32", serial, configuration);

	usb_dev_handle* device = usb_win32_open(serial);

	if (device == NULL) {
		usbmuxd_log(LL_INFO, "Could not find the device %s using libusb-win32", serial, configuration);
		return;
	}

	int res = usb_set_configuration(device, configuration);

	usb_close(device);
}

usb_dev_handle *usb_win32_open(const char serial[])
{
	struct usb_bus *bus;
	struct usb_device *dev;

	usbmuxd_log(LL_INFO, "Finding device %s using libusb-win32", serial);

	bus = usb_get_busses();

	for (bus; bus; bus = bus->next)
	{
		for (dev = bus->devices; dev; dev = dev->next)
		{
			if (dev->descriptor.idVendor != VID_APPLE
				|| dev->descriptor.idProduct < PID_RANGE_LOW
				|| dev->descriptor.idProduct > PID_RANGE_MAX)
			{
				usbmuxd_log(LL_INFO, "Found device %d on bus %d using libusb-win32, but it is not an Apple device. Skipping", dev->devnum, bus->location);
				continue;
			}

			usb_dev_handle *handle = usb_open(dev);

			char dev_serial[40];
			int ret = usb_get_string_simple(handle, dev->descriptor.iSerialNumber, dev_serial, sizeof(dev_serial));

			if(ret < 0) {
				usbmuxd_log(LL_INFO, "Could not get the UDID for device %d on bus %d using libusb-win32. Skipping", dev->devnum, bus->location);
				usb_close(dev);
				continue;
			}

			if (strcmp(dev_serial, serial) != 0)
			{
				usbmuxd_log(LL_INFO, "The not get the UDID for device %d, %s, on bus %d does not match teh requested UDID %s. Skipping", dev->devnum, dev_serial, bus->location, serial);
				usb_close(dev);
				continue;
			}

			return handle;
		}
	}

	return NULL;
}
#endif