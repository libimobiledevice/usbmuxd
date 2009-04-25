/*
 * Copyright (c) 2008 Jing Su. All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA 
 */
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <usb.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include "usbmux.h"

#define BULKIN 0x85
#define BULKOUT 0x04
#define HEADERLEN 28

static const uint8_t TCP_FIN = 1;
static const uint8_t TCP_SYN = 1 << 1;
static const uint8_t TCP_RST = 1 << 2;
static const uint8_t TCP_PSH = 1 << 3;
static const uint8_t TCP_ACK = 1 << 4;
static const uint8_t TCP_URG = 1 << 5;

// I have trouble figuring out how to properly manage the windowing to
// the device. It keeps sending back 512 and seems to drop off a cliff
// when the device gets overwhelmed. In addition, the device likes to
// panic and send out RESETS before the window hits zero. Also, waiting
// for responses seems to not be a winning strategy.
//
// Since I'm not sure how in the hell to interpret the window sizes that
// the device is sending back to us, I've figured out some magic number
// constants which seem to work okay.
static const uint32_t WINDOW_MAX = 5 * 1024;
static const uint32_t WINDOW_INCREMENT = 512;

typedef struct {
	char *buffer;
	int leftover;
	int capacity;
} receivebuf_t;

struct usbmux_device_int {
	char *buffer;
	struct usb_dev_handle *usbdev;
	struct usb_device *__device;
	receivebuf_t usbReceive;
};

typedef struct {
	uint32_t type, length, major, minor, allnull;
} usbmux_version_header;

typedef struct {
	uint32_t type, length;
	uint16_t sport, dport;
	uint32_t scnt, ocnt;
	uint8_t offset, tcp_flags;
	uint16_t window, nullnull, length16;
} usbmux_tcp_header;

struct usbmux_client_int {
	usbmux_tcp_header *header;
	usbmux_device_t device;

	char *recv_buffer;
	int r_len;
	pthread_cond_t wait;

	// this contains a conditional variable which usb-writers can wait
	// on while waiting for window updates from the device.
	pthread_cond_t wr_wait;
	// I'm going to do something really cheesy here. We are going to 
	// just record the most recent scnt that we are expecting to hear
	// back on. We will actually halt progress by limiting the number
	// of outstanding un-acked bulk sends that we have beamed out.
	uint32_t wr_pending_scnt;
	long wr_window;

	pthread_mutex_t mutex;

	// this variable is not protected by the mutex. This will always
	// be E_SUCCESS, unless an error of some kind breaks this stream.
	// this will then be set to the error that caused the broken stream.
	// no further operations other than free_client will be allowed.
	int error;

	int cleanup;
};


static pthread_mutex_t usbmuxmutex = PTHREAD_MUTEX_INITIALIZER;
static usbmux_client_t *connlist = NULL;
static int clients = 0;


/**
 */
int toto_debug = 0;

void usbmux_set_debug(int e)
{
	toto_debug = e;
}

void log_debug_msg(const char *format, ...)
{
#ifndef STRIP_DEBUG_CODE
	va_list args;
	/* run the real fprintf */
	va_start(args, format);

	if (toto_debug)
		vfprintf(stderr, format, args);

	va_end(args);
#endif
}

#ifdef DEBUG
/**
 * for debugging purposes.
 */
static void print_buffer(const char *data, const int length)
{
	if (toto_debug <= 0) {
		return;
	}
	int i;
	int j;
	unsigned char c;

	for (i = 0; i < length; i += 16) {
		printf("%04x: ", i);
		for (j = 0; j < 16; j++) {
			if (i + j >= length) {
				printf("   ");
				continue;
			}
			printf("%02hhx ", *(data + i + j));
		}
		printf("  | ");
		for (j = 0; j < 16; j++) {
			if (i + j >= length)
				break;
			c = *(data + i + j);
			if ((c < 32) || (c > 127)) {
				printf(".");
				continue;
			}
			printf("%c", c);
		}
		printf("\n");
	}
	printf("\n");
}
#endif

void hton_header(usbmux_tcp_header * hdr)
{
	if (hdr) {
		hdr->length = htonl(hdr->length);
		hdr->scnt = htonl(hdr->scnt);
		hdr->ocnt = htonl(hdr->ocnt);
		hdr->length16 = htons(hdr->length16);
	}
}

void ntoh_header(usbmux_tcp_header * hdr)
{
	if (hdr) {
		hdr->length = ntohl(hdr->length);
		hdr->scnt = ntohl(hdr->scnt);
		hdr->ocnt = ntohl(hdr->ocnt);
		hdr->length16 = ntohs(hdr->length16);
	}
}

/** Creates a USBMux header containing version information
 * 
 * @return A USBMux header
 */
usbmux_version_header *version_header()
{
	usbmux_version_header *version =
		(usbmux_version_header *) malloc(sizeof(usbmux_version_header));
	version->type = 0;
	version->length = htonl(20);
	version->major = htonl(1);
	version->minor = 0;
	version->allnull = 0;
	return version;
}

/**
 * This function sets the configuration of the given device to 3
 * and claims the interface 1. If usb_set_configuration fails, it detaches
 * the kernel driver that blocks the device, and retries configuration.
 *
 * @param device which device to configure
 */
static int usbmux_config_usb_device(usbmux_device_t device)
{
	int ret;
	int bytes;
	char buf[512];

#if 0
	log_debug_msg("checking configuration...\n");
	if (device->__device->config->bConfigurationValue != 3) {
		log_debug_msg
			("WARNING: usb device configuration is not 3 as expected!\n");
	}

	log_debug_msg("setting configuration...\n");
	ret = usb_set_configuration(device->device, 3);
	if (ret != 0) {
		log_debug_msg("Hm, usb_set_configuration returned %d: %s\n", ret,
					  strerror(-ret));
#if LIBUSB_HAS_GET_DRIVER_NP
		log_debug_msg("trying to fix:\n");
		log_debug_msg("-> detaching kernel driver... ");
		ret =
			usb_detach_kernel_driver_np(device->device,
										device->__device->config->
										interface->altsetting->
										bInterfaceNumber);
		if (ret != 0) {
			log_debug_msg("usb_detach_kernel_driver_np returned %d: %s\n",
						  ret, strerror(-ret));
		} else {
			log_debug_msg("done.\n");
			log_debug_msg("setting configuration again... ");
			ret = usb_set_configuration(device->device, 3);
			if (ret != 0) {
				log_debug_msg
					("Error: usb_set_configuration returned %d: %s\n", ret,
					 strerror(-ret));
				log_debug_msg("--> trying to continue anyway...\n");
			} else {
				log_debug_msg("done.\n");
			}
		}
#else
		log_debug_msg("--> trying to continue anyway...\n");
#endif
	} else {
		log_debug_msg("done.\n");
	}
#endif

	log_debug_msg("claiming interface... ");
	ret = usb_claim_interface(device->usbdev, 1);
	if (ret != 0) {
		log_debug_msg("Error: usb_claim_interface returned %d: %s\n", ret,
					  strerror(-ret));
		return -ENODEV;
	} else {
		log_debug_msg("done.\n");
	}

	do {
		bytes = usb_bulk_read(device->usbdev, BULKIN, buf, 512, 800);
	} while (bytes > 0);

	return 0;
}

/**
 * Given a USB bus and device number, returns a device handle to the device on
 * that bus. To aid compatibility with future devices, this function does not
 * check the vendor and device IDs! To do that, you should use
 * usbmux_get_device() or a system-specific API (e.g. HAL).
 *
 * @param bus_n The USB bus number.
 * @param dev_n The USB device number.
 * @param device A pointer to a usbmux_device_t, which must be set to NULL upon
 *      calling usbmux_get_specific_device, which will be filled with a device
 *      descriptor on return. 
 * @return 0 if ok, otherwise a negative errno value.
 */
int usbmux_get_specific_device(int bus_n, int dev_n,
							   usbmux_device_t * device)
{
	struct usb_bus *bus;
	struct usb_device *dev;
	usbmux_version_header *version;
	int bytes = 0;

	//check we can actually write in device
	if (!device || (device && *device))
		return -EINVAL;

	usbmux_device_t newdevice =
		(usbmux_device_t) malloc(sizeof(struct usbmux_device_int));

	// Initialize the struct
	newdevice->usbdev = NULL;
	newdevice->__device = NULL;
	newdevice->buffer = NULL;

	// don't forget these:
	newdevice->usbReceive.buffer = NULL;
	newdevice->usbReceive.leftover = 0;
	newdevice->usbReceive.capacity = 0;

	// Initialize libusb
	usb_init();
	usb_find_busses();
	usb_find_devices();

	// Set the device configuration
	for (bus = usb_get_busses(); bus; bus = bus->next)
		//if (bus->location == bus_n)
		for (dev = bus->devices; dev != NULL; dev = dev->next)
			if (dev->devnum == dev_n) {
				newdevice->__device = dev;
				newdevice->usbdev = usb_open(newdevice->__device);
				if (usbmux_config_usb_device(newdevice) == 0) {
					goto found;
				}
			}

	usbmux_free_device(newdevice);

	log_debug_msg("usbmux_get_specific_device: device not found\n");
	return -ENODEV;

  found:
	// Send the version command to the device
	version = version_header();
	bytes =
		usb_bulk_write(newdevice->usbdev, BULKOUT, (char *) version,
					   sizeof(*version), 800);
	if (bytes < 20) {
		log_debug_msg("%s: libusb did NOT send enough!\n", __func__);
		if (bytes < 0) {
			log_debug_msg("%s: libusb gave me the error %d: %s (%s)\n",
						  __func__, bytes, usb_strerror(),
						  strerror(-bytes));
		}
	}
	// Read the device's response
	bytes =
		usb_bulk_read(newdevice->usbdev, BULKIN, (char *) version,
					  sizeof(*version), 800);

	// Check for bad response
	if (bytes < 20) {
		free(version);
		usbmux_free_device(newdevice);
		log_debug_msg("%s: Invalid version message -- header too short.\n",
					  __func__);
		if (bytes < 0) {
			log_debug_msg("%s: libusb error message %d: %s (%s)\n",
						  __func__, bytes, usb_strerror(),
						  strerror(-bytes));
			return bytes;
		}
		return -EBADMSG;
	}
	// Check for correct version
	if (ntohl(version->major) == 1 && ntohl(version->minor) == 0) {
		// We're all ready to roll.
		log_debug_msg("%s: success\n", __func__);
		free(version);
		*device = newdevice;
		return 0;
	} else {
		// Bad header
		usbmux_free_device(newdevice);
		free(version);
		log_debug_msg("%s: Received a bad header/invalid version number.",
					  __func__);
		return -EBADMSG;
	}

	// If it got to this point it's gotta be bad
	log_debug_msg("%s: Unknown error.\n", __func__);
	usbmux_free_device(newdevice);
	free(version);
	return -EBADMSG;			// if it got to this point it's gotta be bad
}

/** Cleans up an usbmux_device_t structure, then frees the structure itself.
 * This is a library-level function; deals directly with the device to tear
 *  down relations, but otherwise is mostly internal.
 * 
 * @param device A pointer to an usbmux_device_t structure.
 */
int usbmux_free_device(usbmux_device_t device)
{
	char buf[512];
	int bytes;

	if (!device)
		return -EINVAL;
	int ret = 0;

	if (device->usbdev) {
		do {
			bytes = usb_bulk_read(device->usbdev, BULKIN, buf, 512, 800);
		} while (bytes > 0);
	}

	if (bytes < 0) {
		ret = bytes;
	}

	if (device->buffer) {
		free(device->buffer);
	}
	if (device->usbReceive.buffer) {
		free(device->usbReceive.buffer);
	}
	if (device->usbdev) {
		usb_release_interface(device->usbdev, 1);
		usb_close(device->usbdev);
		ret = 0;
	}
	free(device);

	return ret;
}



/** Sends data to the device
 * This is a low-level (i.e. directly to device) function.
 * 
 * @param device The device to send data to
 * @param data The data to send
 * @param datalen The length of the data
 * @return The number of bytes sent, or -ERRNO on error
 */
int send_to_device(usbmux_device_t device, char *data, int datalen)
{
	if (!device)
		return -EINVAL;

	int timeout = 1000;
	int retrycount = 0;
	int bytes = 0;

#ifdef DEBUG
#ifdef DEBUG_MORE
	printf("===============================\n%s: trying to send\n",
		   __func__);
	print_buffer(data, datalen);
	printf("===============================\n");
#endif
#endif
	do {
		if (retrycount > 3) {
			log_debug_msg
				("EPIC FAIL! aborting on retry count overload.\n");
			return -ECOMM;
		}

		bytes =
			usb_bulk_write(device->usbdev, BULKOUT, data, datalen,
						   timeout);
		if (bytes == -ETIMEDOUT) {
			// timed out waiting for write.
			log_debug_msg("usb_bulk_write timeout error.\n");
			return bytes;
		} else if (bytes < 0) {
			log_debug_msg
				("usb_bulk_write failed with error. err:%d (%s)(%s)\n",
				 bytes, usb_strerror(), strerror(-bytes));
			return bytes;
		} else if (bytes == 0) {
			log_debug_msg("usb_bulk_write sent nothing. retrying.\n");
			timeout = timeout * 4;
			retrycount++;
			continue;
		} else if (bytes < datalen) {
			log_debug_msg
				("usb_bulk_write failed to send full dataload. %d of %d\n",
				 bytes, datalen);
			timeout = timeout * 4;
			retrycount++;
			data += bytes;
			datalen -= bytes;
			continue;
		}
	} while (0);				// fall out

#ifdef DEBUG
	if (bytes > 0) {
		if (toto_debug > 0) {
			printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
			printf("%s: sent to device\n", __func__);
			print_buffer(data, bytes);
			printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
		}
	}
#endif
	return bytes;
}

/** Receives data from the device
 * This function is a low-level (i.e. direct from device) function.
 * 
 * @param device The device to receive data from
 * @param data Where to put data read
 * @param datalen How much data to read in
 * @param timeout How many milliseconds to wait for data
 * 
 * @return How many bytes were read in, or -1 on error.
 */
int recv_from_device_timeout(usbmux_device_t device, char *data,
							 int datalen, int timeoutmillis)
{
	if (!device)
		return -EINVAL;
	//log_debug_msg("%s: attempting to receive %i bytes\n", __func__, datalen);

	int bytes =
		usb_bulk_read(device->usbdev, BULKIN, data, datalen,
					  timeoutmillis);
	// There are some things which are errors, others which are no problem.
	// It's not documented in libUSB, but it seems that the error values
	// returned are just negated ERRNO values.
	if (bytes < 0) {
		if (bytes == -ETIMEDOUT) {
			// ignore this. it just means timeout reached before we
			//  picked up any data. no problem.
			return 0;
		} else {
			fprintf(stderr, "%s: libusb gave me the error %d: %s (%s)\n",
					__func__, bytes, usb_strerror(), strerror(-bytes));
			log_debug_msg("%s: libusb gave me the error %d: %s (%s)\n",
						  __func__, bytes, usb_strerror(),
						  strerror(-bytes));
		}
		return bytes;
	}
#ifdef DEBUG
	if (bytes > 0) {
		if (toto_debug > 0) {
			printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
			printf("%s: received from device:\n", __func__);
			print_buffer(data, bytes);
			printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
		}
	}
#endif

	return bytes;
}

/** Creates a USBMux packet for the given set of ports.
 * 
 * @param s_port The source port for the connection.
 * @param d_port The destination port for the connection.
 *
 * @return A USBMux packet
 */
usbmux_tcp_header *new_mux_packet(uint16_t s_port, uint16_t d_port)
{
	usbmux_tcp_header *conn =
		(usbmux_tcp_header *) malloc(sizeof(usbmux_tcp_header));
	conn->type = htonl(6);
	conn->length = HEADERLEN;
	conn->sport = htons(s_port);
	conn->dport = htons(d_port);
	conn->scnt = 0;
	conn->ocnt = 0;
	conn->offset = 0x50;
	conn->window = htons(0x0200);
	conn->nullnull = 0x0000;
	conn->length16 = HEADERLEN;
	return conn;
}


/** Removes a connection from the list of connections made.
 * The list of connections is necessary for buffering.
 * 
 * @param connection The connection to delete from the tracking list.
 */
static void delete_connection(usbmux_client_t connection)
{
	usbmux_client_t *newlist = NULL;

	pthread_mutex_lock(&usbmuxmutex);

	// update the global list of connections
	if (clients > 1) {
		newlist =
			(usbmux_client_t *) malloc(sizeof(usbmux_client_t) *
									   (clients - 1));
		int i = 0, j = 0;
		for (i = 0; i < clients; i++) {
			if (connlist[i] == connection)
				continue;
			else {
				newlist[j] = connlist[i];
				j++;
			}
		}
	}
	if (connlist) {
		free(connlist);
	}
	connlist = newlist;
	clients--;

	// free up this connection
	pthread_mutex_lock(&connection->mutex);
	if (connection->recv_buffer)
		free(connection->recv_buffer);
	if (connection->header)
		free(connection->header);
	connection->r_len = 0;
	pthread_mutex_unlock(&connection->mutex);
	pthread_mutex_destroy(&connection->mutex);
	free(connection);

	pthread_mutex_unlock(&usbmuxmutex);
}

/** Adds a connection to the list of connections made.
 * The connection list is necessary for buffering.
 *
 * @param connection The connection to add to the global list of connections.
 */

static void add_connection(usbmux_client_t connection)
{
	pthread_mutex_lock(&usbmuxmutex);
	usbmux_client_t *newlist =
		(usbmux_client_t *) realloc(connlist,
									sizeof(usbmux_client_t) * (clients +
															   1));
	newlist[clients] = connection;
	connlist = newlist;
	clients++;
	pthread_mutex_unlock(&usbmuxmutex);
}

/**
 * Get a source port number that is not used by one of our connections
 * This is needed for us to make sure we are not sending on another
 * connection.
 */
static uint16_t get_free_port()
{
	int i;
	uint16_t newport = 30000;
	int cnt = 0;

	pthread_mutex_lock(&usbmuxmutex);
	while (1) {
		cnt = 0;
		for (i = 0; i < clients; i++) {
			if (ntohs(connlist[i]->header->sport) == newport) {
				cnt++;
			}
		}
		if (cnt == 0) {
			// newport is not used in our list of connections!
			break;
		} else {
			newport++;
			if (newport < 30000) {
				// if all ports from 30000 to 65535 are in use,
				// the value wraps (16-bit overflow)
				// return 0, no port is available.
				// This should not happen, but just in case ;)
				newport = 0;
				break;
			}
		}
	}
	pthread_mutex_unlock(&usbmuxmutex);

	return newport;
}

/** Initializes a connection to 'device' with source port s_port and destination port d_port
 *
 * @param device The device to initialize a connection on.
 * @param src_port The source port
 * @param dst_port The destination port -- 0xf27e for lockdownd. 
 * @param client A mux TCP header for the connection which is used for tracking and data transfer.
 * @return 0 on success, a negative errno value otherwise.
 */
int usbmux_new_client(usbmux_device_t device, uint16_t src_port,
					  uint16_t dst_port, usbmux_client_t * client)
{
	if (!device || !dst_port)
		return -EINVAL;

	src_port = get_free_port();

	if (!src_port) {
		// this is a special case, if we get 0, this is not good, so
		return -EISCONN;		// TODO: error code suitable?
	}
	// Initialize connection stuff
	usbmux_client_t new_connection =
		(usbmux_client_t) malloc(sizeof(struct usbmux_client_int));
	new_connection->header = new_mux_packet(src_port, dst_port);

	// send TCP syn
	if (new_connection && new_connection->header) {
		int err = 0;
		new_connection->header->tcp_flags = TCP_SYN;
		new_connection->header->length = new_connection->header->length;
		new_connection->header->length16 =
			new_connection->header->length16;
		new_connection->header->scnt = 0;
		new_connection->header->ocnt = 0;
		new_connection->device = device;
		new_connection->recv_buffer = NULL;
		new_connection->r_len = 0;
		pthread_cond_init(&new_connection->wait, NULL);
		pthread_mutex_init(&new_connection->mutex, NULL);
		pthread_cond_init(&new_connection->wr_wait, NULL);
		new_connection->wr_pending_scnt = 0;
		new_connection->wr_window = 0;
		add_connection(new_connection);
		new_connection->error = 0;
		new_connection->cleanup = 0;
		hton_header(new_connection->header);
		log_debug_msg("%s: send_to_device (%d --> %d)\n", __func__,
					  ntohs(new_connection->header->sport),
					  ntohs(new_connection->header->dport));
		err =
			send_to_device(device, (char *) new_connection->header,
						   sizeof(usbmux_tcp_header));
		if (err >= 0) {
			*client = new_connection;
			return 0;
		} else {
			delete_connection(new_connection);
			return err;
		}
	}
	// if we get to this point it's probably bad
	return -ENOMEM;
}

/** Cleans up the given USBMux connection.
 * @note Once a connection is closed it may not be used again.
 * 
 * @param connection The connection to close.
 *
 * @return 0 on success or a negative errno value on error.
 */
int usbmux_free_client(usbmux_client_t client)
{
	if (!client || !client->device)
		return -EINVAL;

	int err = 0;
	int result = 0;
	pthread_mutex_lock(&client->mutex);
	client->header->tcp_flags = TCP_FIN;
	client->header->length = 0x1C;
	client->header->window = 0;
	client->header->length16 = 0x1C;
	hton_header(client->header);

	err =
		send_to_device(client->device, (char *) client->header,
					   sizeof(usbmux_tcp_header));
	if (err < 0) {
		log_debug_msg("%s: error sending TCP_FIN\n", __func__);
		result = err;
	}

	client->cleanup = 1;

	// make sure we don't have any last-minute laggards waiting on this.
	// I put it after the mutex unlock because we have cases where the
	// conditional wait is dependent on re-grabbing that mutex.
	pthread_cond_broadcast(&client->wait);
	pthread_cond_destroy(&client->wait);
	pthread_cond_broadcast(&client->wr_wait);
	pthread_cond_destroy(&client->wr_wait);

	pthread_mutex_unlock(&client->mutex);

	return result;
}

/** Sends the given data over the selected connection.
 *
 * @param client The client we're sending data on.
 * @param data A pointer to the data to send.
 * @param datalen How much data we're sending.
 * @param sent_bytes The number of bytes sent, minus the header (28)
 *
 * @return 0 on success or a negative errno value on error.
 */
int usbmux_send(usbmux_client_t client, const char *data, uint32_t datalen,
				uint32_t * sent_bytes)
{
	if (!client->device || !client || !sent_bytes)
		return -EINVAL;

	if (client->error < 0) {
		return client->error;
	}

	*sent_bytes = 0;
	pthread_mutex_lock(&client->mutex);

	int sendresult = 0;
	uint32_t blocksize = 0;
	if (client->wr_window <= 0) {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		//ts.tv_sec += 1;
		ts.tv_nsec += 750 * 1000;
		if (pthread_cond_timedwait(&client->wait, &client->mutex, &ts) ==
			ETIMEDOUT) {
			// timed out. optimistically grow the window and try to make progress
			client->wr_window += WINDOW_INCREMENT;
		}
	}

	blocksize = sizeof(usbmux_tcp_header) + datalen;

	// client->scnt and client->ocnt should already be in host notation...
	// we don't need to change them juuuust yet. 
	char *buffer = (char *) malloc(blocksize + 2);	// allow 2 bytes of safety padding
	// Set the length
	client->header->length = blocksize;
	client->header->length16 = blocksize;

	// Put header into big-endian notation
	hton_header(client->header);
	// Concatenation of stuff in the buffer.
	memcpy(buffer, client->header, sizeof(usbmux_tcp_header));
	memcpy(buffer + sizeof(usbmux_tcp_header), data, datalen);

	log_debug_msg("%s: send_to_device(%d --> %d)\n", __func__,
				  ntohs(client->header->sport),
				  ntohs(client->header->dport));
	sendresult = send_to_device(client->device, buffer, blocksize);
	// Now that we've sent it off, we can clean up after our sloppy selves.
	if (buffer)
		free(buffer);

	// revert header fields that have been swapped before trying to send
	ntoh_header(client->header);

	// update counts ONLY if the send succeeded.
	if ((uint32_t) sendresult == blocksize) {
		// Re-calculate scnt
		client->header->scnt += datalen;
		client->wr_window -= blocksize;
	}

	pthread_mutex_unlock(&client->mutex);

	if (sendresult == -ETIMEDOUT || sendresult == 0) {
		// no problem for now...
		*sent_bytes = 0;
		return -ETIMEDOUT;
	} else if (sendresult < 0) {
		return sendresult;
	} else if ((uint32_t) sendresult == blocksize) {
		// actual number of data bytes sent.
		*sent_bytes = sendresult - HEADERLEN;
		return 0;
	} else {
		fprintf(stderr,
				"usbsend managed to dump a packet that is not full size. %d of %d\n",
				sendresult, blocksize);
		return -EBADMSG;
	}
}

/** append the packet's DATA to the receive buffer for the client.
 *
 *  this has a few other corner-case functions:
 *  1. this will properly handle the handshake syn+ack.
 *  2. for all receives, this will appropriately update the ocnt.
 * 
 * @return number of bytes consumed (header + data)
 */
uint32_t append_receive_buffer(usbmux_client_t client, char *packet)
{
	if (client == NULL || packet == NULL)
		return 0;

	usbmux_tcp_header *header = (usbmux_tcp_header *) packet;
	char *data = &packet[HEADERLEN];
	uint32_t packetlen = ntohl(header->length);
	uint32_t datalen = packetlen - HEADERLEN;

	int dobroadcast = 0;

	pthread_mutex_lock(&client->mutex);

	// we need to handle a few corner case tasks and book-keeping which
	// falls on our responsibility because we are the ones reading in
	// feedback.
	if (client->header->scnt == 0 && client->header->ocnt == 0) {
		log_debug_msg("client is still waiting for handshake.\n");
		if (header->tcp_flags == (TCP_SYN | TCP_ACK)) {
			log_debug_msg("yes, got syn+ack ; replying with ack.\n");
			client->header->tcp_flags = TCP_ACK;
			client->header->length = sizeof(usbmux_tcp_header);
			client->header->length16 = sizeof(usbmux_tcp_header);
			client->header->scnt += 1;
			client->header->ocnt = header->ocnt;
			hton_header(client->header);
			// push it to USB
			// TODO: need to check for error in the send here.... :(
			log_debug_msg("%s: send_to_device (%d --> %d)\n", __func__,
						  ntohs(client->header->sport),
						  ntohs(client->header->dport));
			if (send_to_device
				(client->device, (char *) client->header,
				 sizeof(usbmux_tcp_header)) <= 0) {
				log_debug_msg("%s: error when pushing to usb...\n",
							  __func__);
			}
			// need to revert some of the fields back to host notation.
			ntoh_header(client->header);
		} else {
			client->error = -ECONNABORTED;
			// woah... this connection failed us.
			// TODO: somehow signal that this stream is a no-go.
			log_debug_msg("WOAH! client failed to get proper syn+ack.\n");
		}
	}
	// update TCP counters and windows.
	//
	// save the window that we're getting from the USB device.
	// apparently the window is bigger than just the 512 that's typically
	// advertised. iTunes apparently shifts this value by 8 to get a much
	// larger number.
	if (header->tcp_flags & TCP_RST) {
		client->error = -ECONNRESET;

		if (datalen > 0) {
			char e_msg[128];
			e_msg[0] = 0;
			if (datalen > 1) {
				memcpy(e_msg, data + 1, datalen - 1);
				e_msg[datalen - 1] = 0;
			}
			// fetch the message
			switch (data[0]) {
			case 0:
				// this is not an error, it's just a status message.
				log_debug_msg("received status message: %s\n", e_msg);
				datalen = 0;
				break;
			case 1:
				log_debug_msg("received error message: %s\n", e_msg);
				datalen = 0;
				break;
			default:
				log_debug_msg
					("received unknown message (type 0x%02x): %s\n",
					 data[0], e_msg);
				//datalen = 0; // <-- we let this commented out for testing
				break;
			}
		} else {
			log_debug_msg
				("peer sent connection reset. setting error: %d\n",
				 client->error);
		}
	}
	// the packet's ocnt tells us how much of our data the device has received.
	if (header->tcp_flags & TCP_ACK) {
		// this is a hacky magic number condition. it seems that once
		// the window reported by the device starts to drop below this
		// number, we quickly fall into connection reset problems.
		// Once we see the reported window size start falling off,
		// ut off and wait for solid acks to come back.
		if (ntohs(header->window) < 256)
			client->wr_window = 0;

		// check what just got acked.
		if (ntohl(header->ocnt) < client->header->scnt) {
			// we got some kind of ack, but it hasn't caught up
			// with the pending that have been sent.
			pthread_cond_broadcast(&client->wr_wait);
		} else if (ntohl(header->ocnt) >
				   /*client->wr_pending_scnt */ client->header->scnt) {
			fprintf(stderr,
					"WTF?! acks overtook pending outstanding.  %u,%u\n",
					ntohl(header->ocnt), client->wr_pending_scnt);
		} else {
			// reset the window
			client->wr_window = WINDOW_MAX;
			pthread_cond_broadcast(&client->wr_wait);
		}
	}
	// the packet's scnt will be our new ocnt.
	client->header->ocnt = ntohl(header->scnt);

	// ensure there is enough space, either by first malloc or realloc
	if (datalen > 0) {
		log_debug_msg("%s: putting %d bytes into client's recv_buffer\n",
					  __func__, datalen);
		if (client->r_len == 0)
			dobroadcast = 1;

		if (client->recv_buffer == NULL) {
			client->recv_buffer = malloc(datalen);
			client->r_len = 0;
		} else {
			client->recv_buffer =
				realloc(client->recv_buffer, client->r_len + datalen);
		}

		memcpy(&client->recv_buffer[client->r_len], data, datalen);
		client->r_len += datalen;
	}

	pthread_mutex_unlock(&client->mutex);

	// I put this outside the mutex unlock just so that when the threads
	// wake, we don't have to do another round of unlock+try to grab.
	if (dobroadcast)
		pthread_cond_broadcast(&client->wait);

	return packetlen;
}

/**
 * @note THERE IS NO MUTEX LOCK IN THIS FUNCTION!
 * because we're only called from one location, pullbulk, where the lock
 * is already held.
 */
usbmux_client_t find_client(usbmux_tcp_header * recv_header)
{
	// remember, as we're looking for the client, the receive header is
	// coming from the USB into our client. This means that when we check
	// the src/dst ports, we need to reverse them.
	usbmux_client_t retval = NULL;

	// just for debugging check, I'm going to convert the numbers to host-endian.
	uint16_t hsport = ntohs(recv_header->sport);
	uint16_t hdport = ntohs(recv_header->dport);

	pthread_mutex_lock(&usbmuxmutex);
	int i;
	for (i = 0; i < clients; i++) {
		uint16_t csport = ntohs(connlist[i]->header->sport);
		uint16_t cdport = ntohs(connlist[i]->header->dport);

		if (hsport == cdport && hdport == csport) {
			retval = connlist[i];
			break;
		}
	}
	pthread_mutex_unlock(&usbmuxmutex);

	return retval;
}

/** pull in a big USB bulk packet and distribute it to queues appropriately.
 */
int usbmux_pullbulk(usbmux_device_t device)
{
	if (!device)
		return -EINVAL;

	int res = 0;
	static const int DEFAULT_CAPACITY = 128 * 1024;
	if (device->usbReceive.buffer == NULL) {
		device->usbReceive.capacity = DEFAULT_CAPACITY;
		device->usbReceive.buffer = malloc(device->usbReceive.capacity);
		device->usbReceive.leftover = 0;
	}
	// start the cursor off just ahead of the leftover.
	char *cursor = &device->usbReceive.buffer[device->usbReceive.leftover];
	// pull in content, note that the amount we can pull is capacity minus leftover
	int readlen =
		recv_from_device_timeout(device, cursor,
								 device->usbReceive.capacity -
								 device->usbReceive.leftover, 3000);
	if (readlen < 0) {
		res = readlen;
		//fprintf(stderr, "recv_from_device_timeout gave us an error.\n");
		readlen = 0;
	}
	if (readlen > 0) {
		//fprintf(stdout, "recv_from_device_timeout pulled an extra %d bytes\n", readlen);
	}
	// the amount of content we have to work with is the remainder plus
	// what we managed to read
	device->usbReceive.leftover += readlen;

	// reset the cursor to the front of that buffer and work through
	// trying to decode packets out of them.
	cursor = device->usbReceive.buffer;
	while (1) {
		// check if there's even sufficient data to decode a header
		if (device->usbReceive.leftover < HEADERLEN)
			break;
		usbmux_tcp_header *header = (usbmux_tcp_header *) cursor;

		log_debug_msg("%s: recv_from_device_timeout (%d --> %d)\n",
					  __func__, ntohs(header->sport),
					  ntohs(header->dport));

		// now that we have a header, check if there is sufficient data
		// to construct a full packet, including its data
		uint32_t packetlen = ntohl(header->length);
		if ((uint32_t) device->usbReceive.leftover < packetlen) {
			fprintf(stderr,
					"%s: not enough data to construct a full packet\n",
					__func__);
			break;
		}
		// ok... find the client this packet will get stuffed to.
		usbmux_client_t client = find_client(header);
		if (client == NULL) {
			log_debug_msg
				("WARNING: client for packet cannot be found. dropping packet.\n");
		} else {
			// stuff the data
			log_debug_msg
				("%s: found client, calling append_receive_buffer\n",
				 __func__);
			append_receive_buffer(client, cursor);

			// perhaps this is too general, == -ECONNRESET
			//  might be a better check here
			if (client->error < 0) {
				pthread_mutex_lock(&client->mutex);
				if (client->cleanup) {
					pthread_mutex_unlock(&client->mutex);
					log_debug_msg("freeing up connection (%d->%d)\n",
								  ntohs(client->header->sport),
								  ntohs(client->header->dport));
					delete_connection(client);
				} else {
					pthread_mutex_unlock(&client->mutex);
				}
			}
		}

		// move the cursor and account for the consumption
		cursor += packetlen;
		device->usbReceive.leftover -= packetlen;
	}

	// now, we need to manage any leftovers.
	// I'm going to manage the leftovers by alloc'ing a new block and
	// copyingthe leftovers to it. This is just to prevent problems with
	// memory moves where there may be overlap. Besides, the leftovers
	// should be small enough that this copy is minimal in overhead.
	//
	// if there are no leftovers, we just leave the datastructure as is,
	// and re-use the block next time.
	if (device->usbReceive.leftover > 0
		&& cursor != device->usbReceive.buffer) {
		log_debug_msg("%s: we got a leftover, so handle it\n", __func__);
		char *newbuff = malloc(DEFAULT_CAPACITY);
		memcpy(newbuff, cursor, device->usbReceive.leftover);
		free(device->usbReceive.buffer);
		device->usbReceive.buffer = newbuff;
		device->usbReceive.capacity = DEFAULT_CAPACITY;
	}

	return res;
}

/**
 * return the error code stored in usbmux_client_t structure,
 * e.g. non-zero when an usb read error occurs.
 *
 * @param client the usbmux client
 *
 * @return 0 or a negative errno value.
 */
int usbmux_get_error(usbmux_client_t client)
{
	if (!client) {
		return 0;
	}
	return client->error;
}

/** This function reads from the client's recv_buffer.
 *
 * @param client The client to receive data from.
 * @param data Where to put the data we receive. 
 * @param datalen How much data to read.
 * @param timeout How many milliseconds to wait for data
 *
 * @return 0 on success or a negative errno value on failure.
 */
int usbmux_recv_timeout(usbmux_client_t client, char *data,
						uint32_t datalen, uint32_t * recv_bytes,
						int timeout)
{

	if (!client || !data || datalen == 0 || !recv_bytes)
		return -EINVAL;

	if (client->error < 0)
		return client->error;

	pthread_mutex_lock(&client->mutex);

	if (timeout > 0 && (client->recv_buffer == NULL || client->r_len == 0)) {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += timeout / 1000;
		ts.tv_nsec += (timeout - ((int) (timeout / 1000)) * 1000) * 1000;
		pthread_cond_timedwait(&client->wait, &client->mutex, &ts);
	}

	*recv_bytes = 0;
	if (client->recv_buffer != NULL && client->r_len > 0) {
		uint32_t foolen = datalen;
		if ((int) foolen > client->r_len)
			foolen = client->r_len;
		memcpy(data, client->recv_buffer, foolen);
		*recv_bytes = foolen;

		// preserve any left-over unread amounts.
		int remainder = client->r_len - foolen;
		if (remainder > 0) {
			char *newbuf = malloc(remainder);
			memcpy(newbuf, client->recv_buffer + foolen, remainder);
			client->r_len = remainder;
			free(client->recv_buffer);
			client->recv_buffer = newbuf;
		} else {
			free(client->recv_buffer);
			client->recv_buffer = NULL;
			client->r_len = 0;
		}
	}

	pthread_mutex_unlock(&client->mutex);

	return 0;
}
