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
#include "iphone.h"

#define BULKIN 0x85
#define BULKOUT 0x04
#define HEADERLEN 28

typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint8_t uint8;

static const uint8 TCP_FIN = 1;
static const uint8 TCP_SYN = 1 << 1;
static const uint8 TCP_RST = 1 << 2;
static const uint8 TCP_PSH = 1 << 3;
static const uint8 TCP_ACK = 1 << 4;
static const uint8 TCP_URG = 1 << 5;

// I have trouble figuring out how to properly manage the windowing to
// the iPhone.  It keeps sending back 512 and seems to drop off a cliff
// when the phone gets overwhelmed.  In addition, the phone likes to
// panic and send out RESETS before the window hits zero.  Also, waiting
// for responses seems to not be a winning strategy.
//
// Since I'm not sure how in the hell to interpret the window sizes that
// the phone is sending back to us, I've figured out some magic number
// constants which seem to work okay.
static const uint32 WINDOW_MAX = 5 * 1024;
static const uint32 WINDOW_INCREMENT = 512;

typedef struct {
    char* buffer;
    int leftover;
    int capacity;
} receivebuf_t;

struct iphone_device_int {
	char *buffer;
	struct usb_dev_handle *device;
	struct usb_device *__device;
	receivebuf_t usbReceive;
};

typedef struct {
	uint32 type, length, major, minor, allnull;
} usbmux_version_header;

typedef struct {
	uint32 type, length;
	uint16 sport, dport;
	uint32 scnt, ocnt;
	uint8 offset, tcp_flags;
	uint16 window, nullnull, length16;
} usbmux_tcp_header;

struct iphone_umux_client_int {
	usbmux_tcp_header *header;
	iphone_device_t phone;

	char *recv_buffer;
	int r_len;
    pthread_cond_t wait;

    // this contains a conditional variable which usb-writers can wait
    // on while waiting for window updates from the phone.
    pthread_cond_t wr_wait;
    // I'm going to do something really cheesy here.  We are going to 
    // just record the most recent scnt that we are expecting to hear
    // back on.  We will actually halt progress by limiting the number
    // of outstanding un-acked bulk sends that we have beamed out.
    uint32 wr_pending_scnt;
    long wr_window;

    pthread_mutex_t mutex;

    // this variable is not protected by the mutex.  This will always
    // be E_SUCCESS, unless an error of some kind breaks this stream.
    // this will then be set to the error that caused the broken stream.
    // no further operations other than free_client will be allowed.
    iphone_error_t error;
};


static pthread_mutex_t iphonemutex = PTHREAD_MUTEX_INITIALIZER;
static iphone_umux_client_t *connlist = NULL;
static int clients = 0;
//static receivebuf_t usbReceive = {NULL, 0, 0};


/**
 */
int toto_debug = 0;

void iphone_set_debug(int e)
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
    	int i;
	int j;
	unsigned char c;

	for(i=0; i<length; i+=16) {
		printf("%04x: ", i);
		for (j=0;j<16;j++) {
			if (i+j >= length) {
				printf("   ");
				continue;
			}
			printf("%02hhx ", *(data+i+j));
		}
		printf("  | ");
		for(j=0;j<16;j++) {
			if (i+j >= length)
				break;
			c = *(data+i+j);
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

void hton_header(usbmux_tcp_header *hdr)
{
    if (hdr) {
	hdr->length = htonl(hdr->length);
	hdr->scnt = htonl(hdr->scnt);
	hdr->ocnt = htonl(hdr->ocnt);
	hdr->length16 = htons(hdr->length16);
    }
}

void ntoh_header(usbmux_tcp_header *hdr)
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
	usbmux_version_header *version = (usbmux_version_header *) malloc(sizeof(usbmux_version_header));
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
 * @param phone which device to configure
 */
static iphone_error_t iphone_config_usb_device(iphone_device_t phone)
{
	int ret;
	int bytes;
	char buf[512];

#if 0
	log_debug_msg("checking configuration...\n");
	if (phone->__device->config->bConfigurationValue != 3) {
		log_debug_msg("WARNING: usb device configuration is not 3 as expected!\n");
	}

	log_debug_msg("setting configuration...\n");
	ret = usb_set_configuration(phone->device, 3);
	if (ret != 0) {
		log_debug_msg("Hm, usb_set_configuration returned %d: %s\n", ret, strerror(-ret));
#if LIBUSB_HAS_GET_DRIVER_NP
		log_debug_msg("trying to fix:\n");
		log_debug_msg("-> detaching kernel driver... ");
		ret = usb_detach_kernel_driver_np(phone->device, phone->__device->config->interface->altsetting->bInterfaceNumber);
		if (ret != 0) {
			log_debug_msg("usb_detach_kernel_driver_np returned %d: %s\n", ret, strerror(-ret));
		} else {
			log_debug_msg("done.\n");
			log_debug_msg("setting configuration again... ");
			ret = usb_set_configuration(phone->device, 3);
	                if (ret != 0) {
				log_debug_msg("Error: usb_set_configuration returned %d: %s\n", ret, strerror(-ret));
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
	ret = usb_claim_interface(phone->device, 1);
	if (ret != 0) {
		log_debug_msg("Error: usb_claim_interface returned %d: %s\n", ret, strerror(-ret));
		return IPHONE_E_NO_DEVICE;
	} else {
		log_debug_msg("done.\n");
	}

	do {
		bytes = usb_bulk_read(phone->device, BULKIN, buf, 512, 800);
	} while (bytes > 0);
	
	return IPHONE_E_SUCCESS;
}

/**
 * Given a USB bus and device number, returns a device handle to the iPhone on
 * that bus. To aid compatibility with future devices, this function does not
 * check the vendor and device IDs! To do that, you should use
 * iphone_get_device() or a system-specific API (e.g. HAL).
 *
 * @param bus_n The USB bus number.
 * @param dev_n The USB device number.
 * @param device A pointer to a iphone_device_t, which must be set to NULL upon
 *      calling iphone_get_specific_device, which will be filled with a device
 *      descriptor on return. 
 * @return IPHONE_E_SUCCESS if ok, otherwise an error code.
 */
iphone_error_t iphone_get_specific_device(int bus_n, int dev_n, iphone_device_t * device)
{
	struct usb_bus *bus;
	struct usb_device *dev;
	usbmux_version_header *version;
	int bytes = 0;

	//check we can actually write in device
	if (!device || (device && *device))
		return IPHONE_E_INVALID_ARG;

	iphone_device_t phone = (iphone_device_t) malloc(sizeof(struct iphone_device_int));

	// Initialize the struct
	phone->device = NULL;
	phone->__device = NULL;
	phone->buffer = NULL;

	// don't forget these:
	phone->usbReceive.buffer = NULL;
	phone->usbReceive.leftover = 0;
	phone->usbReceive.capacity = 0;

	// Initialize libusb
	usb_init();
	usb_find_busses();
	usb_find_devices();

	// Set the device configuration
	for (bus = usb_get_busses(); bus; bus = bus->next)
		if (bus->location == bus_n)
			for (dev = bus->devices; dev != NULL; dev = dev->next)
				if (dev->devnum == dev_n) {
					phone->__device = dev;
					phone->device = usb_open(phone->__device);
					if (iphone_config_usb_device(phone) == IPHONE_E_SUCCESS) {
						goto found;
					}
				}

	iphone_free_device(phone);

	log_debug_msg("iphone_get_specific_device: iPhone not found\n");
	return IPHONE_E_NO_DEVICE;

  found:
	// Send the version command to the phone
	version = version_header();
	bytes = usb_bulk_write(phone->device, BULKOUT, (char *) version, sizeof(*version), 800);
	if (bytes < 20) {
		log_debug_msg("get_iPhone(): libusb did NOT send enough!\n");
		if (bytes < 0) {
			log_debug_msg("get_iPhone(): libusb gave me the error %d: %s (%s)\n",
						  bytes, usb_strerror(), strerror(-bytes));
		}
	}
	// Read the phone's response
	bytes = usb_bulk_read(phone->device, BULKIN, (char *) version, sizeof(*version), 800);

	// Check for bad response
	if (bytes < 20) {
		free(version);
		iphone_free_device(phone);
		log_debug_msg("get_iPhone(): Invalid version message -- header too short.\n");
		if (bytes < 0)
			log_debug_msg("get_iPhone(): libusb error message %d: %s (%s)\n", bytes, usb_strerror(), strerror(-bytes));
		return IPHONE_E_NOT_ENOUGH_DATA;
	}
	// Check for correct version
	if (ntohl(version->major) == 1 && ntohl(version->minor) == 0) {
		// We're all ready to roll.
		fprintf(stderr, "get_iPhone() success\n");
		free(version);
		*device = phone;
		return IPHONE_E_SUCCESS;
	} else {
		// Bad header
		iphone_free_device(phone);
		free(version);
		log_debug_msg("get_iPhone(): Received a bad header/invalid version number.");
		return IPHONE_E_BAD_HEADER;
	}

	// If it got to this point it's gotta be bad
	log_debug_msg("get_iPhone(): Unknown error.\n");
	iphone_free_device(phone);
	free(version);
	return IPHONE_E_UNKNOWN_ERROR;	// if it got to this point it's gotta be bad
}


/**
 * Scans all USB busses and devices for a known AFC-compatible device and
 * returns a handle to the first such device it finds. Known devices include
 * those with vendor ID 0x05ac and product ID between 0x1290 and 0x1293
 * inclusive.
 *
 * This function is convenient, but on systems where higher-level abstractions
 * (such as HAL) are available it may be preferable to use
 * iphone_get_specific_device instead, because it can deal with multiple
 * connected devices as well as devices not known to libiphone.
 * 
 * @param device Upon calling this function, a pointer to a location of type
 *  iphone_device_t, which must have the value NULL. On return, this location
 *  will be filled with a handle to the device.
 * @return IPHONE_E_SUCCESS if ok, otherwise an error code.
 */
iphone_error_t iphone_get_device(iphone_device_t * device)
{
	struct usb_bus *bus;
	struct usb_device *dev;

    pthread_mutex_init(&iphonemutex, NULL);

	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_get_busses(); bus != NULL; bus = bus->next)
		for (dev = bus->devices; dev != NULL; dev = dev->next)
			if (dev->descriptor.idVendor == 0x05ac
				&& dev->descriptor.idProduct >= 0x1290 && dev->descriptor.idProduct <= 0x1293)
				return iphone_get_specific_device(bus->location, dev->devnum, device);

	return IPHONE_E_NO_DEVICE;
}

/** Cleans up an iPhone structure, then frees the structure itself.  
 * This is a library-level function; deals directly with the iPhone to tear
 *  down relations, but otherwise is mostly internal.
 * 
 * @param phone A pointer to an iPhone structure.
 */
iphone_error_t iphone_free_device(iphone_device_t device)
{
	char buf[512];
	int bytes;

	if (!device)
		return IPHONE_E_INVALID_ARG;
	iphone_error_t ret = IPHONE_E_UNKNOWN_ERROR;

	if (device->device) {
		do {
			bytes = usb_bulk_read(device->device, BULKIN, buf, 512, 800);
		} while (bytes > 0);
	}

	if (device->buffer) {
		free(device->buffer);
	}
	if (device->usbReceive.buffer) {
		free(device->usbReceive.buffer);
	}
	if (device->device) {
		usb_release_interface(device->device, 1);
		usb_close(device->device);
		ret = IPHONE_E_SUCCESS;
	}
	free(device);
    
    pthread_mutex_destroy(&iphonemutex);

	return ret;
}



/** Sends data to the phone
 * This is a low-level (i.e. directly to phone) function.
 * 
 * @param phone The iPhone to send data to
 * @param data The data to send to the iPhone
 * @param datalen The length of the data
 * @return The number of bytes sent, or -ERRNO on error
 */
int send_to_phone(iphone_device_t phone, char *data, int datalen)
{
	if (!phone)
		return -1;

    int timeout = 1000;
    int retrycount = 0;
    int bytes = 0;

#ifdef DEBUG
    #ifdef DEBUG_MORE
    printf("===============================\n%s: trying to send\n", __func__);
    print_buffer(data, datalen);
    printf("===============================\n");
    #endif
#endif
    do {
        if (retrycount > 3) {
            fprintf(stderr, "EPIC FAIL! aborting on retry count overload.\n");
            return -1;
        }

        bytes = usb_bulk_write(phone->device, BULKOUT, data, datalen, timeout);
        if (bytes == -ETIMEDOUT) {
            // timed out waiting for write.
            fprintf(stderr, "usb_bulk_write timeout error.\n");
            return bytes;
        }
        else if (bytes < 0) {
            fprintf(stderr, "usb_bulk_write failed with error. err:%d (%s)(%s)\n", 
                    bytes, usb_strerror(), strerror(-bytes));
            return -1;
        }
        else if (bytes == 0) {
            fprintf(stderr, "usb_bulk_write sent nothing. retrying.\n");
            timeout = timeout * 4;
            retrycount++;
            continue;
        }
        else if (bytes < datalen) {
            fprintf(stderr, "usb_bulk_write failed to send full dataload. %d of %d\n", bytes, datalen);
            timeout = timeout * 4;
            retrycount++;
            data += bytes;
            datalen -= bytes;
            continue;
        }
    }
    while(0); // fall out

#ifdef DEBUG
    if (bytes > 0) {
	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	printf("%s: sent to phone\n", __func__);
    	print_buffer(data, bytes);
	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
    }
#endif
    return bytes;
}

/**
 */
int recv_from_phone_timeout(iphone_device_t phone, char *data, int datalen, int timeoutmillis)
{
	int bytes = 0;

	if (!phone)
		return -EINVAL;
	//log_debug_msg("recv_from_phone(): attempting to receive %i bytes\n", datalen);

	bytes = usb_bulk_read(phone->device, BULKIN, data, datalen, timeoutmillis);
	if (bytes < 0) {
        // there are some things which are errors, others which are no problem.
        // it's not documented in libUSB, but it seems that the error returns are
        // just negated ERRNO values.
        if (bytes == -ETIMEDOUT) {
            // ignore this.  it just means timeout reached before we
            // picked up any data.  no problem.
	    return 0;
        }
        else {
            fprintf(stderr, "recv_from_phone(): libusb gave me the error %d: %s (%s)\n", bytes, usb_strerror(),
                    strerror(-bytes));
            log_debug_msg("recv_from_phone(): libusb gave me the error %d: %s (%s)\n", bytes, usb_strerror(),
                          strerror(-bytes));
        }
		return bytes;
	}

#ifdef DEBUG
	if (bytes > 0) {
	    printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
	    printf("%s: received from phone:\n", __func__);
	    print_buffer(data, bytes);
	    printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
	}
#endif

	return bytes;
}

/** This function is a low-level (i.e. direct to iPhone) function.
 * 
 * @param phone The iPhone to receive data from
 * @param data Where to put data read
 * @param datalen How much data to read in
 * 
 * @return How many bytes were read in, or -1 on error.
 */
int recv_from_phone(iphone_device_t phone, char *data, int datalen) {
    return recv_from_phone_timeout(phone, data, datalen, 100);
}


/** Creates a USBMux packet for the given set of ports.
 * 
 * @param s_port The source port for the connection.
 * @param d_port The destination port for the connection.
 *
 * @return A USBMux packet
 */
usbmux_tcp_header *new_mux_packet(uint16 s_port, uint16 d_port)
{
	usbmux_tcp_header *conn = (usbmux_tcp_header *) malloc(sizeof(usbmux_tcp_header));
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
static void delete_connection(iphone_umux_client_t connection)
{
    iphone_umux_client_t *newlist = NULL;

    pthread_mutex_lock(&iphonemutex);

    // update the global list of connections
    if (clients > 1) {
	newlist = (iphone_umux_client_t *) malloc(sizeof(iphone_umux_client_t) * (clients - 1));
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

    pthread_mutex_unlock(&iphonemutex);
}

/** Adds a connection to the list of connections made.
 * The connection list is necessary for buffering.
 *
 * @param connection The connection to add to the global list of connections.
 */

static void add_connection(iphone_umux_client_t connection)
{
    pthread_mutex_lock(&iphonemutex);
	iphone_umux_client_t *newlist =
		(iphone_umux_client_t *) realloc(connlist, sizeof(iphone_umux_client_t) * (clients + 1));
	newlist[clients] = connection;
	connlist = newlist;
	clients++;
    pthread_mutex_unlock(&iphonemutex);
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

	pthread_mutex_lock(&iphonemutex);
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
	pthread_mutex_unlock(&iphonemutex);

	return newport;
}

/** Initializes a connection on phone, with source port s_port and destination port d_port
 *
 * @param device The iPhone to initialize a connection on.
 * @param src_port The source port
 * @param dst_port The destination port -- 0xf27e for lockdownd. 
 * @param client A mux TCP header for the connection which is used for tracking and data transfer.
 * @return IPHONE_E_SUCCESS on success, an error code otherwise.
 */
iphone_error_t iphone_mux_new_client(iphone_device_t device, uint16_t src_port, uint16_t dst_port,
									 iphone_umux_client_t * client)
{
	if (!device || !dst_port)
		return IPHONE_E_INVALID_ARG;

	src_port = get_free_port();

	if (!src_port) {
		// this is a special case, if we get 0, this is not good, so
		return -EISCONN; // TODO: error code suitable?
	}

	// Initialize connection stuff
	iphone_umux_client_t new_connection = (iphone_umux_client_t) malloc(sizeof(struct iphone_umux_client_int));
	new_connection->header = new_mux_packet(src_port, dst_port);

	// send TCP syn
	if (new_connection && new_connection->header) {
		new_connection->header->tcp_flags = TCP_SYN;
		new_connection->header->length = new_connection->header->length;
		new_connection->header->length16 = new_connection->header->length16;
        new_connection->header->scnt = 0;
        new_connection->header->ocnt = 0;
        new_connection->phone = device;
        new_connection->recv_buffer = NULL;
        new_connection->r_len = 0;
        pthread_cond_init(&new_connection->wait, NULL);
        pthread_mutex_init(&new_connection->mutex, NULL);
        pthread_cond_init(&new_connection->wr_wait, NULL);
        new_connection->wr_pending_scnt = 0;
        new_connection->wr_window = 0;
        add_connection(new_connection);
        new_connection->error = IPHONE_E_SUCCESS;
		hton_header(new_connection->header);
		log_debug_msg("%s: send_to_phone (%d --> %d)\n", __func__, ntohs(new_connection->header->sport), ntohs(new_connection->header->dport));
		if (send_to_phone(device, (char *) new_connection->header, sizeof(usbmux_tcp_header)) >= 0) {
            *client = new_connection;
            return IPHONE_E_SUCCESS;
		} else {
            delete_connection(new_connection);
			return IPHONE_E_NOT_ENOUGH_DATA;
		}
	}
	// if we get to this point it's probably bad
	return IPHONE_E_UNKNOWN_ERROR;
}

/** Cleans up the given USBMux connection.
 * @note Once a connection is closed it may not be used again.
 * 
 * @param connection The connection to close.
 *
 * @return IPHONE_E_SUCCESS on success.
 */
iphone_error_t iphone_mux_free_client(iphone_umux_client_t client)
{
	if (!client || !client->phone)
		return IPHONE_E_INVALID_ARG;

    pthread_mutex_lock(&client->mutex);
	client->header->tcp_flags = TCP_FIN;
	client->header->length = 0x1C;
	client->header->window = 0;
	client->header->length16 = 0x1C;
	hton_header(client->header);
	int bytes = 0;

	bytes = usb_bulk_write(client->phone->device, BULKOUT, (char *) client->header, sizeof(usbmux_tcp_header), 800);
	if (bytes < 0)
		log_debug_msg("iphone_mux_free_client(): when writing, libusb gave me the error: %s\n", usb_strerror());

	bytes = usb_bulk_read(client->phone->device, BULKIN, (char *) client->header, sizeof(usbmux_tcp_header), 800);
	if (bytes < 0)
		log_debug_msg("get_iPhone(): when reading, libusb gave me the error: %s\n", usb_strerror());
    
    pthread_mutex_unlock(&client->mutex);
    // make sure we don't have any last-minute laggards waiting on this.
    // I put it after the mutex unlock because we have cases where the
    // conditional wait is dependent on re-grabbing that mutex.
    pthread_cond_broadcast(&client->wait);
    pthread_cond_destroy(&client->wait);
    pthread_cond_broadcast(&client->wr_wait);
    pthread_cond_destroy(&client->wr_wait);

	delete_connection(client);

	return IPHONE_E_SUCCESS;
}


/** Sends the given data over the selected connection.
 *
 * @param phone The iPhone to send to.
 * @param client The client we're sending data on.
 * @param data A pointer to the data to send.
 * @param datalen How much data we're sending.
 * @param sent_bytes The number of bytes sent, minus the header (28)
 *
 * @return IPHONE_E_SUCCESS on success.
 */
iphone_error_t iphone_mux_send(iphone_umux_client_t client, const char *data, uint32_t datalen, uint32_t * sent_bytes)
{
	if (!client->phone || !client || !sent_bytes)
		return IPHONE_E_INVALID_ARG;

    if (client->error != IPHONE_E_SUCCESS) {
        return client->error;
    }

    *sent_bytes = 0;
    pthread_mutex_lock(&client->mutex);

    int sendresult = 0;
    uint32 blocksize = 0;
    if (client->wr_window <= 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        //ts.tv_sec += 1;
        ts.tv_nsec += 750 * 1000;
        if (pthread_cond_timedwait(&client->wait, &client->mutex, &ts) == ETIMEDOUT) {
            // timd out.  optimistically grow the window and try to make progress
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

    log_debug_msg("%s: send_to_phone(%d --> %d)\n", __func__, ntohs(client->header->sport), ntohs(client->header->dport));
    sendresult = send_to_phone(client->phone, buffer, blocksize);
    // Now that we've sent it off, we can clean up after our sloppy selves.
    if (buffer)
        free(buffer);

    // revert header fields that have been swapped before trying to send
    ntoh_header(client->header);

    // update counts ONLY if the send succeeded.
    if (sendresult == blocksize) {
        // Re-calculate scnt
	client->header->scnt += datalen;
        client->wr_window -= blocksize;
    }


    pthread_mutex_unlock(&client->mutex);

    
    if (sendresult == -ETIMEDOUT || sendresult == 0) {
        // no problem for now...
        *sent_bytes = 0;
        return IPHONE_E_TIMEOUT;
    }
    else if (sendresult < 0) {
        return IPHONE_E_UNKNOWN_ERROR;
    }
    else if (sendresult == blocksize) {
        // actual number of data bytes sent.
        *sent_bytes = sendresult - HEADERLEN;
        return IPHONE_E_SUCCESS;
    }
    else {
        fprintf(stderr, "usbsend managed to dump a packet that is not full size. %d of %d\n", 
                sendresult, blocksize);
        return IPHONE_E_UNKNOWN_ERROR;
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
uint32 append_receive_buffer(iphone_umux_client_t client, char* packet)
{
    if (client == NULL || packet == NULL) return 0;

    usbmux_tcp_header *header = (usbmux_tcp_header *) packet;
    char* data = &packet[HEADERLEN];
    uint32 packetlen = ntohl(header->length);
    uint32 datalen = packetlen-HEADERLEN;

    int dobroadcast = 0;

    pthread_mutex_lock(&client->mutex);

    // we need to handle a few corner case tasks and book-keeping which
    // falls on our responsibility because we are the ones reading in
    // feedback.
    if (client->header->scnt == 0 && client->header->ocnt == 0 ) {
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
	    log_debug_msg("%s: send_to_phone (%d --> %d)\n", __func__, ntohs(client->header->sport), ntohs(client->header->dport));
	    if (send_to_phone(client->phone, (char *)client->header, sizeof(usbmux_tcp_header)) <= 0) {
		log_debug_msg("%s: error when pushing to usb...\n", __func__);
	    }
            // need to revert some of the fields back to host notation.
	    ntoh_header(client->header);
        }
        else {
            client->error = IPHONE_E_ECONNABORTED;
            // woah... this connection failed us.
            // TODO: somehow signal that this stream is a no-go.
            log_debug_msg("WOAH! client failed to get proper syn+ack.\n");
        }
    }

    // update TCP counters and windows.
    //
    // save the window that we're getting from the USB device.
    // apparently the window is bigger than just the 512 that's typically
    // advertised.  iTunes apparently shifts this value by 8 to get a much
    // larger number.
    if (header->tcp_flags & TCP_RST) {
        client->error = IPHONE_E_ECONNRESET;

	if (datalen > 0) {
	    char e_msg[128];
	    e_msg[0] = 0;
	    if (datalen > 1) {
		memcpy(e_msg, data+1, datalen-1);
		e_msg[datalen-1] = 0;
	    }	    
	    // fetch the message
	    switch(data[0]) {
		case 0:
		    // this is not an error, it's just a status message.
		    fprintf(stdout, "received status message: %s\n", e_msg);
		    datalen = 0;
		    break;
		case 1:
		    fprintf(stderr, "received error message: %s\n", e_msg);
		    datalen = 0;
		    break;
		default:
		    fprintf(stderr, "received unknown message (type 0x%02x): %s\n", data[0], e_msg);
		    //datalen = 0; // <-- we let this commented out for testing
		    break;
	    }
	} else {
	    fprintf(stderr, "peer sent connection reset. setting error: %d\n", client->error);
	}
    }

    // the packet's ocnt tells us how much of our data the device has received.
    if (header->tcp_flags & TCP_ACK) {
        
        // this is a hacky magic number condition.  it seems that once the window
        // reported by the phone starts to drop below this number, we quickly fall
        // into connection reset problems.  Once we see the reported window size
        // start falling off, cut off and wait for solid acks to come back.
        if (ntohs(header->window) < 256) 
            client->wr_window = 0;

        // check what just got acked.
        if (ntohl(header->ocnt) < client->header->scnt) {
            // we got some kind of ack, but it hasn't caught up with the
            // pending that have been sent.
            pthread_cond_broadcast(&client->wr_wait);
        }
        else if (ntohl(header->ocnt) > /*client->wr_pending_scnt*/ client->header->scnt) {
            fprintf(stderr, "WTF?! acks overtook pending outstanding.  %u,%u\n",
                    ntohl(header->ocnt), client->wr_pending_scnt);
        }
        else {
            // reset the window
            client->wr_window = WINDOW_MAX;
            pthread_cond_broadcast(&client->wr_wait);
        }
    }
    
    // the packet's scnt will be our new ocnt.  
    client->header->ocnt = ntohl(header->scnt);

    // ensure there is enough space, either by first malloc or realloc
    if (datalen > 0) {
	log_debug_msg("%s: putting %d bytes into client's recv_buffer\n", __func__, datalen);
        if (client->r_len == 0) dobroadcast = 1;

        if (client->recv_buffer == NULL) {
            client->recv_buffer = malloc(datalen);
            client->r_len = 0;
        }
        else {
            client->recv_buffer = realloc(client->recv_buffer, client->r_len + datalen);
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

/** NOTE!  THERE IS NO MUTEX LOCK IN THIS FUNCTION!
    because we're only called from one location, pullbulk, where the lock
    is already held.
 */
iphone_umux_client_t find_client(usbmux_tcp_header* recv_header)
{
    // remember, as we're looking for the client, the receive header is
    // coming from the USB into our client.  This means that when we check
    // the src/dst ports, we need to reverse them.
    iphone_umux_client_t retval = NULL;

    // just for debugging check, I'm going to convert the numbers to host-endian.
    uint16 hsport = ntohs(recv_header->sport);
    uint16 hdport = ntohs(recv_header->dport);

    pthread_mutex_lock(&iphonemutex);
    int i;
    for (i = 0; i < clients; i++) {
        uint16 csport = ntohs(connlist[i]->header->sport);
        uint16 cdport = ntohs(connlist[i]->header->dport);

        if (hsport == cdport  && hdport == csport) {
            retval = connlist[i];
            break;
        }
    }
    pthread_mutex_unlock(&iphonemutex);

    return retval;
}

/** pull in a big USB bulk packet and distribute it to queues appropriately.
 */
int iphone_mux_pullbulk(iphone_device_t phone)
{
    if (!phone) {
	fprintf(stderr, "iphone_mux_pullbulk: invalid argument\n");
	return -EINVAL;
    }

    int res = 0;
    static const int DEFAULT_CAPACITY = 128*1024;
    if (phone->usbReceive.buffer == NULL) {
        phone->usbReceive.capacity = DEFAULT_CAPACITY;
        phone->usbReceive.buffer = malloc(phone->usbReceive.capacity);
        phone->usbReceive.leftover = 0;
    }

    // start the cursor off just ahead of the leftover.
    char* cursor = &phone->usbReceive.buffer[phone->usbReceive.leftover];
    // pull in content, note that the amount we can pull is capacity minus leftover
    int readlen = recv_from_phone_timeout(phone, cursor, phone->usbReceive.capacity - phone->usbReceive.leftover, 5000);
    if (readlen < 0) {
	res = readlen;
        //fprintf(stderr, "recv_from_phone_timeout gave us an error.\n");
        readlen = 0;
    }
    if (readlen > 0) {
        //fprintf(stdout, "recv_from_phone_timeout pulled an extra %d bytes\n", readlen);	
    }

    // the amount of content we have to work with is the remainder plus
    // what we managed to read
    phone->usbReceive.leftover += readlen;

    // reset the cursor to the front of that buffer and work through
    // trying to decode packets out of them.
    cursor = phone->usbReceive.buffer;
    while (1) {
        // check if there's even sufficient data to decode a header
        if (phone->usbReceive.leftover < HEADERLEN) break;
        usbmux_tcp_header *header = (usbmux_tcp_header *) cursor;

    	log_debug_msg("%s: recv_from_phone_timeout (%d --> %d)\n", __func__, ntohs(header->sport), ntohs(header->dport));

        // now that we have a header, check if there is sufficient data
        // to construct a full packet, including its data
        uint32 packetlen = ntohl(header->length);
        if (phone->usbReceive.leftover < packetlen) {
	    fprintf(stderr, "%s: not enough data to construct a full packet\n", __func__);
            break;
        }

        // ok... find the client this packet will get stuffed to.
        iphone_umux_client_t client = find_client(header);
        if (client == NULL) {
            log_debug_msg("WARNING: client for packet cannot be found. dropping packet.\n");
        }
        else {
            // stuff the data
	    log_debug_msg("%s: found client, calling append_receive_buffer\n", __func__);
            append_receive_buffer(client, cursor);
        }

        // move the cursor and account for the consumption
        cursor += packetlen;
        phone->usbReceive.leftover -= packetlen;
    }
    
    // now, we need to manage any leftovers.
    // I'm going to manage the leftovers by alloc'ing a new block and copying
    // the leftovers to it.  This is just to prevent problems with memory
    // moves where there may be overlap.  Besides, the leftovers should be
    // small enough that this copy is minimal in overhead.
    //
    // if there are no leftovers, we just leave the datastructure as is,
    // and re-use the block next time.
    if (phone->usbReceive.leftover > 0 && cursor != phone->usbReceive.buffer) {
	fprintf(stderr, "%s: we got a leftover, so handle it\n", __func__);
        char* newbuff = malloc(DEFAULT_CAPACITY);
        memcpy(newbuff, cursor, phone->usbReceive.leftover);
        free(phone->usbReceive.buffer);
        phone->usbReceive.buffer = newbuff;
        phone->usbReceive.capacity = DEFAULT_CAPACITY;
    }

    return res;
}    

/**
 * return the error code stored in iphone_umux_client_t structure,
 * e.g. non-zero when an usb read error occurs.
 *
 * @param client the umux client
 *
 * @return IPHONE_E_* error codes.
 */
iphone_error_t iphone_mux_get_error(iphone_umux_client_t client)
{
    if (!client) {
	return 0;
    }

    return client->error;
}

/** This is a higher-level USBMuxTCP-like function
 *
 * @param connection The connection to receive data on.
 * @param data Where to put the data we receive. 
 * @param datalen How much data to read.
 *
 * @return IPHONE_E_SUCCESS or error code if failure.
 */
iphone_error_t iphone_mux_recv(iphone_umux_client_t client, char *data, uint32_t datalen, uint32_t * recv_bytes)
{
    return  iphone_mux_recv_timeout(client, data, datalen, recv_bytes, 0);
}

/**
   @param timeout
 */
iphone_error_t iphone_mux_recv_timeout(iphone_umux_client_t client, char *data, uint32_t datalen, uint32_t * recv_bytes, int timeout)
{

	if (!client || !data || datalen == 0 || !recv_bytes)
		return IPHONE_E_INVALID_ARG;

    if (client->error != IPHONE_E_SUCCESS) return client->error;

    pthread_mutex_lock(&client->mutex);

    if (timeout > 0 && (client->recv_buffer == NULL ||client->r_len == 0)) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout/1000;
        ts.tv_nsec += (timeout-((int)(timeout/1000))*1000)*1000;  //millis * 1000;
        pthread_cond_timedwait(&client->wait, &client->mutex, &ts);
    }
        
    *recv_bytes = 0;
    if (client->recv_buffer != NULL && client->r_len > 0) {
        uint32_t foolen = datalen;
        if (foolen > client->r_len) foolen = client->r_len;
        memcpy(data, client->recv_buffer, foolen);
        *recv_bytes = foolen;
            
        // preserve any left-over unread amounts.
        int remainder = client->r_len - foolen;
        if (remainder > 0) {
            char* newbuf = malloc(remainder);
            memcpy(newbuf, client->recv_buffer + foolen, remainder);
            client->r_len = remainder;
            free(client->recv_buffer);
            client->recv_buffer = newbuf;
        }
        else {
            free(client->recv_buffer);
            client->recv_buffer = NULL;
            client->r_len = 0;
        }
    }
    
    pthread_mutex_unlock(&client->mutex);
    

    return IPHONE_E_SUCCESS;
}
