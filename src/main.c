/*
 * usbmuxd -- daemon for communication with iPhone/iPod via USB
 * 
 * Copyright (c) 2009 Nikias Bassen. All Rights Reserved.
 * Based upon iTunnel source code, Copyright (c) 2008 Jing Su.
 *  http://www.cs.toronto.edu/~jingsu/itunnel/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA 
 */
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdarg.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <usb.h>
#include <pwd.h>

#include "usbmuxd-proto.h"
#include "sock_stuff.h"

#include "usbmux.h"

#define DEFAULT_TIMEOUT 4000
#define DEFAULT_CHILDREN_CAPACITY 10
#define DEBUG_LEVEL 0

#define LOCKFILE "/var/run/usbmuxd.lock"

#define THREAD (unsigned int)pthread_self()

static int quit_flag = 0;
static int fsock = -1;
static int verbose = DEBUG_LEVEL;
static int foreground = 0;
static int exit_on_no_devices = 0;

struct device_info {
	uint32_t device_id;
	usbmux_device_t phone;
	int use_count;
	pthread_t bulk_reader;
	pthread_mutex_t mutex;
	/* mutex for mutual exclusion of calling the usbmux_send function
	 * TODO: I don't know if we need really need this? */
	pthread_mutex_t writer_mutex;
};

struct client_data {
	volatile int dead;
	int socket;
	int tag;
	pthread_t thread;
	pthread_t handler;
	pthread_t reader;
	int reader_quit;
	int reader_dead;
	int handler_dead;
	int connected;
	usbmux_client_t muxclient;
	struct device_info *dev;
};

static struct device_info **devices = NULL;
static int device_count = 0;
static pthread_mutex_t usbmux_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t usb_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * logs a message to syslog when running as daemon or to stdout/stderr when
 * running in foreground.
 * @param prio The logging priority.
 * @param format The message to be printed.
 */
static void logmsg(int prio, const char *format, ...)
{
	va_list args;
	va_start(args, format);

	if (!foreground) {
		// daemon. log using syslog.
		vsyslog(prio, format, args);
	} else {
		// running in foreground. log to stdout/stderr.
		char msgbuf[256];
		FILE *lfp = stdout;
		switch (prio) {
		case LOG_EMERG:
		case LOG_ALERT:
		case LOG_CRIT:
		case LOG_ERR:
		case LOG_WARNING:
			lfp = stderr;
			break;
		default:
			lfp = stdout;
		}
		strcpy(msgbuf, "usbmuxd: ");
		vsnprintf(msgbuf + 9, 244, format, args);
		strcat(msgbuf, "\n");
		fputs(msgbuf, lfp);
	}

	va_end(args);
}

#ifdef DEBUG
/**
 * for debugging purposes.
 */
static void print_buffer(FILE * fp, const char *data, const int length)
{
	int i;
	int j;
	unsigned char c;

	for (i = 0; i < length; i += 16) {
		if (verbose >= 4)
			fprintf(fp, "%04x: ", i);
		for (j = 0; j < 16; j++) {
			if (i + j >= length) {
				if (verbose >= 4)
					fprintf(fp, "   ");
				continue;
			}
			if (verbose >= 4)
				fprintf(fp, "%02hhx ", *(data + i + j));
		}
		if (verbose >= 4)
			fprintf(fp, "  | ");
		for (j = 0; j < 16; j++) {
			if (i + j >= length)
				break;
			c = *(data + i + j);
			if ((c < 32) || (c > 127)) {
				if (verbose >= 4)
					fprintf(fp, ".");
				continue;
			}
			if (verbose >= 4)
				fprintf(fp, "%c", c);
		}
		if (verbose >= 4)
			fprintf(fp, "\n");
	}
	if (verbose >= 4)
		fprintf(fp, "\n");
}
#endif

/**
 * Read incoming usbmuxd packet. If the packet is larger than
 * the size specified by len, the data will be truncated.
 *
 * @param fd the file descriptor to read from.
 * @param data pointer to a buffer to store the read data to.
 * @param len the length of the data to be read. The buffer
 *        pointed to by data should be at least len bytes in size.
 *
 * @return 
 */
static int usbmuxd_get_request(int fd, void **data, size_t len)
{
	uint32_t pktlen;
	int recv_len;

	if (peek_buf(fd, &pktlen, sizeof(pktlen)) < (int) sizeof(pktlen)) {
		return -errno;
	}

	if (len == 0) {
		// allocate buffer space
		*data = malloc(pktlen);
	} else if (len < pktlen) {
		// target buffer is to small to hold this packet! fix it!
		if (verbose >= 2)
			logmsg(LOG_WARNING,
				   "%s: WARNING -- packet (%d) is larger than target buffer (%d)! Truncating.",
				   __func__, pktlen, len);
		pktlen = len;
	}

	recv_len = recv_buf(fd, *data, pktlen);
	if ((recv_len > 0) && ((uint32_t) recv_len < pktlen)) {
		if (verbose >= 2)
			logmsg(LOG_WARNING,
				   "%s: Uh-oh, we got less than the packet's size, %d instead of %d...",
				   __func__, recv_len, pktlen);
	}
#ifdef DEBUG
	if (*data && (recv_len > 0) && verbose >= 4) {
		fprintf(stderr, "%s: received:\n", __func__);
		print_buffer(stderr, *data, recv_len);
	}
#endif

	return recv_len;
}

/**
 * Send a usbmuxd result packet with given tag and result_code.
 *
 * @param fd the file descriptor to write to.
 * @param tag the tag value that identifies where this message belongs to.
 * @param result_code the error value (0 = Success, most likely errno values otherwise)
 *
 * @return the return value returned by send_buf (normally the number of bytes sent)
 */
static int usbmuxd_send_result(int fd, uint32_t tag, uint32_t result_code)
{
	struct usbmuxd_result res;
	int ret;

	res.header.length = sizeof(res);
	res.header.reserved = 0;
	res.header.type = USBMUXD_RESULT;
	res.header.tag = tag;
	res.result = result_code;

	if (verbose >= 4)
		logmsg(LOG_NOTICE, "%s: tag=%d result=%d", __func__,
			   res.header.tag, res.result);

	ret = send_buf(fd, &res, sizeof(res));
	fsync(fd);					// let's get it sent
	return ret;
}

/**
 * this thread reads from the usb connection and writes the 
 * data to the connected client.
 *
 * @param arg pointer to a client_data structure.
 *
 * @return NULL in any case
 */
static void *usbmuxd_client_reader_thread(void *arg)
{
	struct client_data *cdata;

	char rbuffer[512];
	uint32_t rbuffersize = 512;
	uint32_t rlen;
	int err;
	char *cursor;
	ssize_t len;
	int result;

	if (!arg) {
		if (verbose >= 2)
			logmsg(LOG_ERR, "%s: invalid client_data supplied!", __func__);
		cdata->reader_dead = 1;
		return NULL;
	}

	cdata = (struct client_data *) arg;

	cdata->reader_dead = 0;

	if (verbose >= 3)
		logmsg(LOG_NOTICE, "%s[%d:%d]: started", __func__,
			   cdata->dev->device_id, cdata->dev->use_count);

	while (!quit_flag && !cdata->reader_quit) {
		result = check_fd(cdata->socket, FD_WRITE, DEFAULT_TIMEOUT);
		if (result <= 0) {
			if (result < 0) {
				if (verbose >= 2)
					logmsg(LOG_ERR, "%s: select error: %s", __func__,
						   strerror(errno));
			}
			continue;
		}

		rlen = 0;
		err =
			usbmux_recv_timeout(cdata->muxclient, rbuffer, rbuffersize,
								&rlen, DEFAULT_TIMEOUT);
		if (err != 0) {
			if (verbose >= 2)
				logmsg(LOG_ERR,
					   "%s[%d:%d]: encountered USB read error: %d",
					   __func__, cdata->dev->device_id,
					   cdata->dev->use_count, err);
			break;
		}

		cursor = rbuffer;
		while (rlen > 0) {
			len = send_buf(cdata->socket, cursor, rlen);
			if (len <= 0) {
				logmsg(LOG_ERR, "%s: Error: send returned %d", __func__,
					   len);
				err = 1;
				break;
			}
			// calculate remainder
			rlen -= len;
			// advance cursor
			cursor += len;
		}
		if (err != 0) {
			logmsg(LOG_ERR, "%s: Error when writing to client...",
				   __func__);
			break;
		}
		fsync(cdata->socket);
	}

	if (verbose >= 3)
		logmsg(LOG_NOTICE, "%s[%d:%d]: terminated", __func__,
			   cdata->dev->device_id, cdata->dev->use_count);

	cdata->reader_dead = 1;

	return NULL;
}

/**
 * This function handles the connecting procedure to a previously
 * set up usbmux client.
 * Sends a usbmuxd result packet denoting success or failure.
 * A successful result is mandatory for later communication.
 *
 * @param cdata pointer to a previously initialized client_data structure
 *
 * @return
 */
static int usbmuxd_handleConnectResult(struct client_data *cdata)
{
	int result;
	char buffer[512];
	char err_type[64];
	int err_code;
	ssize_t maxlen = 512;
	uint32_t rlen;
	int err;

	if (!cdata) {
		if (verbose >= 2)
			logmsg(LOG_ERR, "%s: Invalid client_data provided!", __func__);
		return -EINVAL;
	}

	result = check_fd(cdata->socket, FD_WRITE, DEFAULT_TIMEOUT);
	if (result <= 0) {
		if (result < 0) {
			if (verbose >= 2)
				logmsg(LOG_ERR, "%s: select error: %s", __func__,
					   strerror(errno));
			return result;
		}
	} else {
		result = 0;
		err =
			usbmux_recv_timeout(cdata->muxclient, buffer, maxlen, &rlen,
								100);
		if (err < 0) {
			if (verbose >= 2)
				logmsg(LOG_ERR, "%s: encountered USB read error: %d",
					   __func__, err);
			usbmuxd_send_result(cdata->socket, cdata->tag, -err);
			return err;
		} else {
			if (rlen > 0) {
				if ((buffer[0] == 1) && (rlen > 20)
					&& !memcmp(buffer + 1, "handleConnectResult:", 20)) {
					// hm... we got an error message!
					buffer[rlen] = 0;
					if (verbose >= 1)
						logmsg(LOG_ERR, "%s: %s\n", __func__, buffer + 22);

					if (sscanf
						(buffer + 22, "%s - %d\n", err_type, &err_code)
						== 2) {
						usbmuxd_send_result(cdata->socket, cdata->tag,
											err_code);
						return -err_code;
					} else {
						usbmuxd_send_result(cdata->socket, cdata->tag,
											ENODATA);
						return -ENODATA;
					}
				} else {
					// send success result
					usbmuxd_send_result(cdata->socket, cdata->tag, 0);
					// and the server greeting message
					send_buf(cdata->socket, buffer, rlen);
				}
			} else {
				// no server greeting? this seems to be ok. send success.
				usbmuxd_send_result(cdata->socket, cdata->tag, 0);
			}
		}
		//fsync(cdata->socket);
	}
	return result;
}

/**
 * This thread handles the communication between the connected iPhone/iPod
 * and the client that created the connection.
 */
static void *usbmuxd_client_handler_thread(void *arg)
{
	struct client_data *cdata;
	int result;
	char *cursor;
	char buffer[65536];
	ssize_t len;
	ssize_t maxlen = sizeof(buffer);
	uint32_t wlen;
	int err;

	if (!arg) {
		if (verbose >= 2)
			logmsg(LOG_ERR, "%s: invalid client_data provided!", __func__);
		return NULL;
	}

	cdata = (struct client_data *) arg;

	if (verbose >= 3)
		logmsg(LOG_NOTICE, "%s[%d:%d]: started", __func__,
			   cdata->dev->device_id, cdata->dev->use_count);

	if (usbmuxd_handleConnectResult(cdata)) {
		if (verbose >= 3)
			logmsg(LOG_ERR, "handleConnectResult: Error");
		goto leave;
	} else {
		if (verbose >= 3)
			logmsg(LOG_NOTICE, "handleConnectResult: Success");
	}

	// starting mux reader thread
	cdata->reader_quit = 0;
	cdata->reader_dead = 0;
	if (pthread_create
		(&cdata->reader, NULL, usbmuxd_client_reader_thread, cdata) != 0) {
		if (verbose >= 2)
			logmsg(LOG_ERR, "%s: could not start client_reader thread",
				   __func__);
		cdata->reader = 0;
	}

	while (!quit_flag && !cdata->reader_dead) {
		result = check_fd(cdata->socket, FD_READ, DEFAULT_TIMEOUT);
		if (result <= 0) {
			if (result < 0) {
				if (verbose >= 3)
					logmsg(LOG_ERR, "%s: Error: checkfd: %s", __func__,
						   strerror(errno));
			}
			continue;
		}
		// check_fd told us there's data available, so read from client
		// and push to USB device.
		len = recv(cdata->socket, buffer, maxlen, 0);
		if (len == 0) {
			break;
		}
		if (len < 0) {
			if (verbose >= 2)
				logmsg(LOG_ERR, "%s[%d:%d]: Error: recv: %s", __func__,
					   cdata->dev->device_id, cdata->dev->use_count,
					   strerror(errno));
			break;
		}

		cursor = buffer;

		pthread_mutex_lock(&cdata->dev->writer_mutex);
		do {
			wlen = 0;
			err = usbmux_send(cdata->muxclient, cursor, len, &wlen);
			if (err == -ETIMEDOUT) {
				// some kind of timeout... just be patient and retry.
			} else if (err < 0) {
				if (verbose >= 2)
					logmsg(LOG_ERR, "%s[%d:%d]: USB write error: %d",
						   __func__, cdata->dev->device_id,
						   cdata->dev->use_count, err);
				len = -1;
				break;
			}
			// calculate remainder.
			len -= wlen;
			// advance cursor appropiately.
			cursor += wlen;
		}
		while ((len > 0) && !quit_flag);
		pthread_mutex_unlock(&cdata->dev->writer_mutex);
		if (len < 0) {
			break;
		}
	}

  leave:
	// cleanup
	if (verbose >= 3)
		logmsg(LOG_NOTICE, "%s[%d:%d]: terminating", __func__,
			   cdata->dev->device_id, cdata->dev->use_count);
	if (cdata->reader != 0) {
		cdata->reader_quit = 1;
		pthread_join(cdata->reader, NULL);
	}

	cdata->handler_dead = 1;

	if (verbose >= 3)
		logmsg(LOG_NOTICE, "%s[%d:%d]: terminated", __func__,
			   cdata->dev->device_id, cdata->dev->use_count);
	return NULL;
}

/**
 * Thread performing usb_bulk_read from the connected device.
 * One thread per device. Lives as long as the device is in use.
 */
static void *usbmuxd_bulk_reader_thread(void *arg)
{
	struct device_info *cur_dev;
	int err;

	if (!arg) {
		if (verbose >= 2)
			logmsg(LOG_ERR, "%s: Invalid client_data provided", __func__);
		return NULL;
	}

	cur_dev = (struct device_info *) arg;

	if (verbose >= 3)
		logmsg(LOG_NOTICE, "%s: started", __func__);

	while (!quit_flag && cur_dev) {

		pthread_mutex_lock(&cur_dev->mutex);
		if (cur_dev->use_count <= 0) {
			pthread_mutex_unlock(&cur_dev->mutex);
			break;
		}
		pthread_mutex_unlock(&cur_dev->mutex);

		if ((err = usbmux_pullbulk(cur_dev->phone)) < 0) {
			if (verbose >= 1)
				logmsg(LOG_ERR, "%s: error %d when reading from device",
					   __func__, err);
			break;
		}
	}

	if (verbose >= 3)
		logmsg(LOG_NOTICE, "%s: terminated", __func__);

	return NULL;
}

/**
 * This thread is started when a new connection is accepted.
 * It performs the handshake, then waits for the connect packet and
 * on success it starts the usbmuxd_client_handler thread.
 */
static void *usbmuxd_client_init_thread(void *arg)
{
	struct client_data *cdata;
	struct usbmuxd_scan_request *s_req = NULL;
	struct usbmuxd_device_info_record dev_info_rec;
	struct usbmuxd_connect_request *c_req = NULL;

	struct usb_bus *bus;
	struct usb_device *dev;

	int recv_len;
	int found = 0;
	int res;
	int i;

	usbmux_device_t phone = NULL;
	struct device_info *cur_dev = NULL;

	if (!arg) {
		if (verbose >= 1)
			logmsg(LOG_ERR, "%s[%x]: invalid client_data provided!",
				   __func__, THREAD);
		return NULL;
	}

	cdata = (struct client_data *) arg;
	cdata->dead = 0;

	if (verbose >= 3)
		logmsg(LOG_NOTICE, "%s[%x]: started (fd=%d)", __func__, THREAD,
			   cdata->socket);

	if ((recv_len =
		 usbmuxd_get_request(cdata->socket, (void **) &s_req, 0)) <= 0) {
		if (verbose >= 2)
			logmsg(LOG_ERR, "%s[%x]: No scan packet received, error %s",
				   __func__, THREAD, strerror(errno));
		goto leave;
	}

	if ((recv_len == sizeof(struct usbmuxd_scan_request))
		&& (s_req->header.length == sizeof(struct usbmuxd_scan_request))
		&& (s_req->header.reserved == 0)
		&& (s_req->header.type == USBMUXD_SCAN)) {
		// send success response
		if (verbose >= 3)
			logmsg(LOG_NOTICE, "%s[%x]: Got scan packet!", __func__,
				   THREAD);
		usbmuxd_send_result(cdata->socket, s_req->header.tag, 0);
	} else if ((recv_len == sizeof(struct usbmuxd_connect_request))
			   && (s_req->header.type == USBMUXD_CONNECT)) {
		c_req = (struct usbmuxd_connect_request *) s_req;
		s_req = NULL;
		goto connect;
	} else {
		// send error response and exit
		if (verbose >= 2)
			logmsg(LOG_ERR, "%s[%x]: Invalid scan packet received.",
				   __func__, THREAD);
		// TODO is this required?!
		usbmuxd_send_result(cdata->socket, s_req->header.tag, EINVAL);
		goto leave;
	}

	pthread_mutex_lock(&usb_mutex);
	// gather data about all iPhones/iPods attached

	if (verbose >= 5)
		logmsg(LOG_DEBUG, "%s[%x]: usb init", __func__, THREAD);
	usb_init();
	if (verbose >= 5)
		logmsg(LOG_DEBUG, "%s[%x]: usb find busses", __func__, THREAD);
	usb_find_busses();
	if (verbose >= 5)
		logmsg(LOG_DEBUG, "%s[%x]: usb find devices", __func__, THREAD);
	usb_find_devices();

	if (verbose >= 2)
		logmsg(LOG_NOTICE, "%s[%x]: Looking for attached devices...",
			   __func__, THREAD);

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if (dev->descriptor.idVendor == 0x05ac
				&& dev->descriptor.idProduct >= 0x1290
				&& dev->descriptor.idProduct <= 0x1293) {
				if (verbose >= 1)
					logmsg(LOG_NOTICE,
						   "%s[%x]: Found device on bus %d, id %d",
						   __func__, THREAD, bus->location, dev->devnum);
				found++;

				// construct packet
				memset(&dev_info_rec, 0, sizeof(dev_info_rec));
				dev_info_rec.header.length = sizeof(dev_info_rec);
				dev_info_rec.header.type = USBMUXD_DEVICE_INFO;
				dev_info_rec.device.device_id = dev->devnum;
				dev_info_rec.device.product_id = dev->descriptor.idProduct;
				if (dev->descriptor.iSerialNumber) {
					usb_dev_handle *udev;
					//pthread_mutex_lock(&usbmux_mutex);
					udev = usb_open(dev);
					if (udev) {
						usb_get_string_simple(udev,
											  dev->descriptor.
											  iSerialNumber,
											  dev_info_rec.device.
											  serial_number,
											  sizeof(dev_info_rec.device.
													 serial_number) + 1);
						usb_close(udev);
					}
					//pthread_mutex_unlock(&usbmux_mutex);
				}
#ifdef DEBUG
				if (verbose >= 4)
					print_buffer(stderr, (char *) &dev_info_rec,
								 sizeof(dev_info_rec));
#endif

				// send it
				if (send_buf
					(cdata->socket, &dev_info_rec,
					 sizeof(dev_info_rec)) <= 0) {
					if (verbose >= 3)
						logmsg(LOG_ERR,
							   "%s[%x]: Error: Could not send device info: %s",
							   __func__, THREAD, strerror(errno));
					found--;
				}
			}
		}
	}
	pthread_mutex_unlock(&usb_mutex);

	if (found <= 0) {
		if (verbose >= 1)
			logmsg(LOG_NOTICE,
				   "%s[%x]: No attached iPhone/iPod devices found.",
				   __func__, THREAD);
		goto leave;
	}

	if (verbose >= 3)
		logmsg(LOG_NOTICE, "%s[%x]: Waiting for connect request", __func__,
			   THREAD);

	// now wait for connect request
	//memset(&c_req, 0, sizeof(c_req));
	if ((recv_len =
		 usbmuxd_get_request(cdata->socket, (void **) &c_req, 0)) <= 0) {
		if (verbose >= 3)
			logmsg(LOG_NOTICE,
				   "%s[%x]: Did not receive any connect request.",
				   __func__, THREAD);
		goto leave;
	}

  connect:

	if (c_req->header.type != USBMUXD_CONNECT) {
		if (verbose >= 2)
			logmsg(LOG_ERR,
				   "%s[%x]: Unexpected packet of type %d received.",
				   __func__, THREAD, c_req->header.type);
		goto leave;
	}

	if (verbose >= 3)
		logmsg(LOG_NOTICE,
			   "%s[%x]: Setting up connection to usb device #%d on port %d",
			   __func__, THREAD, c_req->device_id,
			   ntohs(c_req->tcp_dport));

	// find the device, and open usb connection
	pthread_mutex_lock(&usbmux_mutex);
	phone = NULL;
	cur_dev = NULL;
	// first check if we already have an open connection
	if (devices) {
		for (i = 0; i < device_count; i++) {
			if (devices[i]) {
				if (devices[i]->device_id == c_req->device_id) {
					devices[i]->use_count++;
					cur_dev = devices[i];
					phone = cur_dev->phone;
					break;
				}
			}
		}
	}
	if (!phone) {
		// if not found, make a new connection
		if (verbose >= 2)
			logmsg(LOG_NOTICE,
				   "%s[%x]: creating new usb connection, device_id=%d",
				   __func__, THREAD, c_req->device_id);

		pthread_mutex_lock(&usb_mutex);
		if (usbmux_get_specific_device(0, c_req->device_id, &phone) < 0) {
			pthread_mutex_unlock(&usb_mutex);
			pthread_mutex_unlock(&usbmux_mutex);
			if (verbose >= 1)
				logmsg(LOG_ERR, "%s[%x]: device_id %d could not be opened",
					   __func__, THREAD, c_req->device_id);
			usbmuxd_send_result(cdata->socket, c_req->header.tag, ENODEV);
			goto leave;
		}
		pthread_mutex_unlock(&usb_mutex);

		// create device object
		if (verbose >= 3)
			logmsg(LOG_DEBUG, "%s[%x]: add to device list", __func__,
				   THREAD);
		cur_dev =
			(struct device_info *) malloc(sizeof(struct device_info));
		memset(cur_dev, 0, sizeof(struct device_info));
		cur_dev->use_count = 1;
		cur_dev->device_id = c_req->device_id;
		cur_dev->phone = phone;
		cur_dev->bulk_reader = 0;
		pthread_mutex_init(&cur_dev->mutex, NULL);
		pthread_mutex_init(&cur_dev->writer_mutex, NULL);

		if (verbose >= 3)
			logmsg(LOG_DEBUG, "%s[%x]: device_count = %d", __func__,
				   THREAD, device_count);

		// add to list of devices
		devices =
			(struct device_info **) realloc(devices,
											sizeof(struct device_info *) *
											(device_count + 1));
		if (devices) {
			devices[device_count] = cur_dev;
			device_count++;
		}
	} else {
		if (verbose >= 2)
			logmsg(LOG_NOTICE,
				   "%s[%x]: reusing usb connection, device_id=%d",
				   __func__, THREAD, c_req->device_id);
	}
	pthread_mutex_unlock(&usbmux_mutex);

	// setup connection to iPhone/iPod
//    pthread_mutex_lock(&usbmux_mutex);
	res =
		usbmux_new_client(cur_dev->phone, 0, ntohs(c_req->tcp_dport),
						  &(cdata->muxclient));
//    pthread_mutex_unlock(&usbmux_mutex);

	if (res != 0) {
		usbmuxd_send_result(cdata->socket, c_req->header.tag, res);
		if (verbose >= 1)
			logmsg(LOG_ERR,
				   "%s[%x]: mux_new_client returned %d, aborting.",
				   __func__, THREAD, res);
		goto leave;
	}
	// start bulk reader thread (once per device)
	pthread_mutex_lock(&cur_dev->mutex);
	if (cur_dev->bulk_reader == 0) {
		pthread_create(&cur_dev->bulk_reader, NULL,
					   usbmuxd_bulk_reader_thread, cur_dev);
	}
	pthread_mutex_unlock(&cur_dev->mutex);

	// start connection handler thread
	cdata->handler_dead = 0;
	cdata->tag = c_req->header.tag;
	cdata->dev = cur_dev;
	if (pthread_create
		(&cdata->handler, NULL, usbmuxd_client_handler_thread, cdata) != 0)
	{
		if (verbose >= 1)
			logmsg(LOG_ERR,
				   "%s[%x]: could not create usbmuxd_client_handler_thread!",
				   __func__, THREAD);
		cdata->handler = 0;
		goto leave;
	}
	// wait for handler thread to finish its work
	if (cdata->handler != 0) {
		pthread_join(cdata->handler, NULL);
	}

	if (verbose >= 2)
		logmsg(LOG_NOTICE, "%s[%x]: closing connection", __func__, THREAD);

	// time to clean up
	if (cdata && cdata->muxclient) {	// should be non-NULL
		usbmux_free_client(cdata->muxclient);
	}

  leave:
	if (verbose >= 3)
		logmsg(LOG_NOTICE, "%s[%x]: terminating", __func__, THREAD);

	if (s_req) {
		free(s_req);
	}
	if (c_req) {
		free(c_req);
	}
	// this has to be freed only if it's not in use anymore as it closes
	// the USB connection
	pthread_mutex_lock(&usbmux_mutex);
	if (cur_dev) {
		pthread_mutex_lock(&cur_dev->mutex);
		if (cur_dev->use_count > 1) {
			if (verbose >= 2)
				logmsg(LOG_NOTICE,
					   "%s[%x]: decreasing device use count (from %d to %d)",
					   __func__, THREAD, cur_dev->use_count,
					   cur_dev->use_count - 1);
			cur_dev->use_count--;
			pthread_mutex_unlock(&cur_dev->mutex);
		} else {
			if (verbose >= 2)
				logmsg(LOG_NOTICE,
					   "%s[%x]: last client disconnected, cleaning up",
					   __func__, THREAD);
			cur_dev->use_count = 0;
			pthread_mutex_unlock(&cur_dev->mutex);
			if (cur_dev->bulk_reader != 0) {
				if (verbose >= 3)
					logmsg(LOG_NOTICE, "%s[%x]: joining bulk_reader...",
						   __func__, THREAD);
				pthread_join(cur_dev->bulk_reader, NULL);
			}
			pthread_mutex_lock(&usb_mutex);
			usbmux_free_device(cur_dev->phone);
			pthread_mutex_unlock(&usb_mutex);
			pthread_mutex_destroy(&cur_dev->writer_mutex);
			pthread_mutex_destroy(&cur_dev->mutex);
			free(cur_dev);
			cur_dev = NULL;
			if (device_count > 1) {
				struct device_info **newlist;
				int j;

				newlist =
					(struct device_info **)
					malloc(sizeof(struct device_info *)
						   * device_count - 1);
				for (i = 0; i < device_count; i++) {
					if (devices[i] != NULL) {
						newlist[j++] = devices[i];
					}
				}
				free(devices);
				devices = newlist;
				device_count--;
			} else {
				free(devices);
				devices = NULL;
				device_count = 0;
			}
		}
	}
	pthread_mutex_unlock(&usbmux_mutex);

	cdata->dead = 1;
	close(cdata->socket);

	if (verbose >= 3)
		logmsg(LOG_NOTICE, "%s[%x]: terminated", __func__, THREAD);

	return NULL;
}

/**
 * make this program run detached from the current console
 */
static int daemonize()
{
	pid_t pid;
	pid_t sid;

	// already a daemon
	if (getppid() == 1)
		return 0;

	pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}

	if (pid > 0) {
		// exit parent process
		exit(EXIT_SUCCESS);
	}
	// At this point we are executing as the child process

	// Change the file mode mask
	umask(0);

	// Create a new SID for the child process
	sid = setsid();
	if (sid < 0) {
		return -1;
	}
	// Change the current working directory.
	if ((chdir("/")) < 0) {
		return -2;
	}
	// Redirect standard files to /dev/null
	if (!freopen("/dev/null", "r", stdin)) {
		logmsg(LOG_ERR, "ERROR: redirection of stdin failed.\n");
	}
	if (!freopen("/dev/null", "w", stdout)) {
		logmsg(LOG_ERR, "ERROR: redirection of stdout failed.\n");
	}
	if (!freopen("/dev/null", "w", stderr)) {
		logmsg(LOG_ERR, "ERROR: redirection of stderr failed.\n");
	}

	return 0;
}

static void usage()
{
	printf("usage: usbmuxd [options]\n");
	printf("\t-h|--help        print this message.\n");
	printf("\t-v|--verbose     be verbose\n");
	printf("\t-f|--foreground  do not daemonize\n");
	printf("\n");
}

static void parse_opts(int argc, char **argv)
{
	static struct option longopts[] = {
		{"help", 0, NULL, 'h'},
		{"foreground", 0, NULL, 'f'},
		{"verbose", 0, NULL, 'v'},
		{"exit-on-no-devices", 0, NULL, 'e'},
		{NULL, 0, NULL, 0}
	};
	int c;

	while (1) {
		c = getopt_long(argc, argv, "hfve", longopts, (int *) 0);
		if (c == -1) {
			break;
		}

		switch (c) {
		case 'h':
			usage();
			exit(0);
		case 'f':
			foreground = 1;
			break;
		case 'v':
			sock_stuff_set_verbose(++verbose);
			break;
		case 'e':
			exit_on_no_devices = 1;
			break;
		default:
			usage();
			exit(2);
		}
	}
}

/**
 * checks for attached devices
 *
 * @return number of devices found
 */
static int devices_attached()
{
	struct usb_bus *bus;
	struct usb_device *dev;
	int res = 0;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if (dev->descriptor.idVendor == 0x05ac
				&& dev->descriptor.idProduct >= 0x1290
				&& dev->descriptor.idProduct <= 0x1293) {
				res++;
			}
		}
	}

	return res;
}

/**
 * signal handler function for cleaning up properly
 */
static void handle_signal(int sig)
{
	if (sig == SIGTERM) {
		quit_flag = 1;
	} else {
		if (sig == SIGINT) {
			if (verbose >= 1)
				fprintf(stderr, "CTRL+C pressed\n");
		}

		if (verbose >= 1)
			fprintf(stderr, "Checking if we can terminate (no more devices attached)...\n");

		if (devices_attached() > 0) {
			// we can't quit, there are still devices attached.
			if (verbose >= 1)
				fprintf(stderr, "Refusing to terminate, there are still devices attached. Kill me with signal 15 (TERM) to force quit.\n");
		} else {
			// it's safe to quit
			quit_flag = 1;
		}
	}
}

/**
 * main function. Initializes all stuff and then loops waiting in accept.
 */
int main(int argc, char **argv)
{
	struct sockaddr_un c_addr;
	socklen_t len = sizeof(struct sockaddr_un);
	struct client_data *cdata = NULL;
	struct client_data **children = NULL;
	int children_capacity = DEFAULT_CHILDREN_CAPACITY;
	int i;
	int result = 0;
	int cnt = 0;
	FILE *lfd = NULL;
	struct flock lock;

	parse_opts(argc, argv);

	argc -= optind;
	argv += optind;

	if (!foreground) {
		openlog("usbmuxd", LOG_PID, 0);
	}

	if (verbose >= 2)
		logmsg(LOG_NOTICE, "starting");

	// signal(SIGHUP, reload_conf); // none yet
	signal(SIGINT, handle_signal);
	signal(SIGQUIT, handle_signal);
	signal(SIGTERM, handle_signal);
	signal(SIGPIPE, SIG_IGN);

	// check for other running instance
	lfd = fopen(LOCKFILE, "r");
	if (lfd) {
		lock.l_type = 0;
		lock.l_whence = SEEK_SET;
		lock.l_start = 0;
		lock.l_len = 0;
		fcntl(fileno(lfd), F_GETLK, &lock);
		fclose(lfd);
		if (lock.l_type != F_UNLCK) {
			logmsg(LOG_NOTICE,
				   "another instance is already running. exiting.");
			return -1;
		}
	}

	if (exit_on_no_devices) {
		if (devices_attached() <= 0) {
			logmsg(LOG_NOTICE, "no devices attached. exiting.");
			return 0;
		}
	}

	fsock = create_unix_socket(USBMUXD_SOCKET_FILE);
	if (fsock < 0) {
		logmsg(LOG_ERR, "Could not create socket, exiting");
		if (!foreground) {
			closelog();
		}
		return -1;
	}

	chmod(USBMUXD_SOCKET_FILE, 0666);

	if (verbose >= 3)
		usbmux_set_debug(1);

	if (!foreground) {
		if (daemonize() < 0) {
			fprintf(stderr, "usbmuxd: FATAL: Could not daemonize!\n");
			syslog(LOG_ERR, "FATAL: Could not daemonize!");
			closelog();
			exit(EXIT_FAILURE);
		}
	}
	// now open the lockfile and place the lock
	lfd = fopen(LOCKFILE, "w");
	if (lfd) {
		lock.l_type = F_WRLCK;
		lock.l_whence = SEEK_SET;
		lock.l_start = 0;
		lock.l_len = 0;
		if (fcntl(fileno(lfd), F_SETLK, &lock) == -1) {
			logmsg(LOG_ERR, "ERROR: lockfile locking failed!");
		}
	}
	// drop elevated privileges
	if (getuid() == 0 || geteuid() == 0) {
		struct passwd *pw = getpwnam("nobody");
		if (pw) {
			setuid(pw->pw_uid);
		} else {
			logmsg(LOG_ERR,
				   "ERROR: Dropping privileges failed, check if user 'nobody' exists! Will now terminate.");
			exit(EXIT_FAILURE);
		}

		// security check
		if (setuid(0) != -1) {
			logmsg(LOG_ERR, "ERROR: Failed to drop privileges properly!");
			exit(EXIT_FAILURE);
		}
		if (verbose >= 2)
			logmsg(LOG_NOTICE, "Successfully dropped privileges");
	}
	// Reserve space for 10 clients which should be enough. If not, the
	// buffer gets enlarged later.
	children =
		(struct client_data **) malloc(sizeof(struct client_data *) *
									   children_capacity);
	if (!children) {
		logmsg(LOG_ERR,
			   "Out of memory when allocating memory for child threads. Terminating.");
		if (!foreground) {
			closelog();
		}
		exit(EXIT_FAILURE);
	}
	memset(children, 0, sizeof(struct client_data *) * children_capacity);

	if (verbose >= 2)
		logmsg(LOG_NOTICE, "waiting for connection");
	while (!quit_flag) {
		// Check the file descriptor before accepting a connection.
		// If no connection attempt is made, just repeat...
		result = check_fd(fsock, FD_READ, 1000);
		if (result <= 0) {
			if (result == 0) {
				// cleanup
				for (i = 0; i < children_capacity; i++) {
					if (children[i]) {
						if (children[i]->dead != 0) {
							pthread_join(children[i]->thread, NULL);
							if (verbose >= 3)
								logmsg(LOG_NOTICE,
									   "reclaimed client thread (fd=%d)",
									   children[i]->socket);
							free(children[i]);
							children[i] = NULL;
							cnt++;
						} else {
							cnt = 0;
						}
					} else {
						cnt++;
					}
				}

				if ((children_capacity > DEFAULT_CHILDREN_CAPACITY)
					&& ((children_capacity - cnt) <=
						DEFAULT_CHILDREN_CAPACITY)) {
					children_capacity = DEFAULT_CHILDREN_CAPACITY;
					children =
						realloc(children,
								sizeof(struct client_data *) *
								children_capacity);
				}
				continue;
			} else {
				if (verbose >= 3)
					logmsg(LOG_ERR, "usbmuxd: select error: %s",
						   strerror(errno));
				continue;
			}
		}

		cdata = (struct client_data *) malloc(sizeof(struct client_data));
		memset(cdata, 0, sizeof(struct client_data));
		if (!cdata) {
			quit_flag = 1;
			logmsg(LOG_ERR, "Error: Out of memory! Terminating.");
			break;
		}

		cdata->socket = accept(fsock, (struct sockaddr *) &c_addr, &len);
		if (cdata->socket < 0) {
			free(cdata);
			if (errno == EINTR) {
				continue;
			} else {
				if (verbose >= 3)
					logmsg(LOG_ERR, "Error in accept: %s",
						   strerror(errno));
				continue;
			}
		}

		if (verbose >= 1)
			logmsg(LOG_NOTICE, "new client connected (fd=%d)",
				   cdata->socket);

		// create client thread:
		if (pthread_create
			(&cdata->thread, NULL, usbmuxd_client_init_thread, cdata) == 0)
		{
			for (i = 0; i < children_capacity; i++) {
				if (children[i] == NULL)
					break;
			}
			if (i == children_capacity) {
				// enlarge buffer
				children_capacity++;
				children =
					realloc(children,
							sizeof(struct client_data *) *
							children_capacity);
				if (!children) {
					logmsg(LOG_ERR,
						   "Out of memory when enlarging child thread buffer");
				}
			}
			children[i] = cdata;
		} else {
			logmsg(LOG_ERR, "Failed to create client_init_thread.");
			close(cdata->socket);
			free(cdata);
			cdata = NULL;
		}
	}

	if (verbose >= 3)
		logmsg(LOG_NOTICE, "terminating");

	// preparing for shutdown: wait for child threads to terminate (if any)
	if (verbose >= 2)
		logmsg(LOG_NOTICE, "waiting for child threads to terminate...");
	for (i = 0; i < children_capacity; i++) {
		if (children[i] != NULL) {
			pthread_join(children[i]->thread, NULL);
			free(children[i]);
			children[i] = NULL;
		}
	}

	// delete the children set.
	free(children);
	children = NULL;


	if (fsock >= 0) {
		close(fsock);
	}

	unlink(USBMUXD_SOCKET_FILE);

	// unlock lock file and close it.
	if (lfd) {
		lock.l_type = F_UNLCK;
		fcntl(fileno(lfd), F_SETLK, &lock);
		fclose(lfd);
	}

	if (verbose >= 1)
		logmsg(LOG_NOTICE, "usbmuxd: terminated");
	if (!foreground) {
		closelog();
	}

	return 0;
}
