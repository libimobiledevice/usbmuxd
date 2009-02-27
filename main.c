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
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <usb.h>

#include "usbmuxd.h"
#include "sock_stuff.h"

#include "iphone.h"

#define DEFAULT_TIMEOUT 4000
#define DEFAULT_CHILDREN_CAPACITY 10

static int quit_flag = 0;
static int fsock = -1;

struct device_use_info {
    uint32_t device_id;
    iphone_device_t phone;
    int use_count;
    pthread_t bulk_reader;
    pthread_mutex_t mutex;
    /* mutex for mutual exclusion of calling the iphone_mux_send function
     * TODO: I don't know if we need really need this? */
    pthread_mutex_t writer_mutex;
    /* mutex to keep the reader threads from reading partial packages */
    //pthread_mutex_t reader_mutex;
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
    iphone_umux_client_t muxclient;
    struct device_use_info *duinfo;
};

static struct device_use_info **device_use_list = NULL;
static int device_use_count = 0;
static pthread_mutex_t usbmux_mutex = PTHREAD_MUTEX_INITIALIZER;

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
static int usbmuxd_get_request(int fd, void *data, size_t len)
{
    uint32_t pktlen;
    int recv_len;

    if (peek_buf(fd, &pktlen, sizeof(pktlen)) < sizeof(pktlen)) {
	return -errno;
    }

    if (len < pktlen) {
	// target buffer is to small to hold this packet! fix it!
	fprintf(stderr, "%s: WARNING -- packet (%d) is larger than target buffer (%d)! Truncating.\n", __func__, pktlen, len);
	pktlen = len;
    }

    recv_len = recv_buf(fd, data, pktlen);
    if ((recv_len > 0) && (recv_len < pktlen)) {
	fprintf(stderr, "%s: Uh-oh, we got less than the packet's size, %d instead of %d...\n", __func__, recv_len, pktlen);
    }

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

    res.header.length = sizeof(res);
    res.header.reserved = 0;
    res.header.type = USBMUXD_RESULT;
    res.header.tag = tag;
    res.result = result_code;

    fprintf(stderr, "%s: tag=%d result=%d\n", __func__, res.header.tag, res.result);

    return send_buf(fd, &res, sizeof(res));
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
    iphone_error_t err;
    char *cursor;
    ssize_t len;
    int result;

    if (!arg) {
	fprintf(stderr, "%s: invalid client_data supplied!\n", __func__);
	cdata->reader_dead = 1;
	return NULL;
    }

    cdata = (struct client_data*)arg;

    cdata->reader_dead = 0;

    fprintf(stdout, "%s[%d:%d]: started\n", __func__, cdata->duinfo->device_id, cdata->duinfo->use_count);

    while (!quit_flag && !cdata->reader_quit) {
	result = check_fd(cdata->socket, FD_WRITE, DEFAULT_TIMEOUT);
	if (result <= 0) {
	    if (result < 0) {
		fprintf(stderr, "%s: select error: %s\n", __func__, strerror(errno));
	    }
	    continue;
	}

	rlen = 0;
	//pthread_mutex_lock(&cdata->duinfo->reader_mutex);
	err = iphone_mux_recv_timeout(cdata->muxclient, rbuffer, rbuffersize, &rlen, DEFAULT_TIMEOUT);
	//pthread_mutex_unlock(&cdata->duinfo->reader_mutex);
	if (err != 0) {
	    fprintf(stderr, "%s[%d:%d]: encountered USB read error: %d\n", __func__, cdata->duinfo->device_id, cdata->duinfo->use_count, err);
	    break;
	}

	cursor = rbuffer;
	while (rlen > 0) {
    	    len = send_buf(cdata->socket, cursor, rlen);
	    // calculate remainder
	    rlen -= len;
	    // advance cursor
	    cursor += len;
	}
	fsync(cdata->socket);
    }

    fprintf(stdout, "%s[%d:%d]: terminated\n", __func__, cdata->duinfo->device_id, cdata->duinfo->use_count);

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
    iphone_error_t err;

    if (!cdata) {
	fprintf(stderr, "%s: Invalid client_data provided!\n", __func__);
	return -EINVAL;
    }

    result = check_fd(cdata->socket, FD_WRITE, DEFAULT_TIMEOUT);
    if (result <= 0) {
	if (result < 0) {
	    fprintf(stderr, "%s: select error: %s\n", __func__, strerror(errno));
	    return result;
	}
    } else {
	result = 0;
	//pthread_mutex_lock(&cdata->duinfo->reader_mutex);
	err = iphone_mux_recv_timeout(cdata->muxclient, buffer, maxlen, &rlen, DEFAULT_TIMEOUT);
	//pthread_mutex_unlock(&cdata->duinfo->reader_mutex);
	if (err != 0) {
	    fprintf(stderr, "%s: encountered USB read error: %d\n", __func__, err);
	    usbmuxd_send_result(cdata->socket, cdata->tag, -err);
	    return err;
	} else {
	    if (rlen > 0) {
		if ((buffer[0] == 1) && (rlen > 20) && !memcmp(buffer+1, "handleConnectResult:", 20)) {
		    // hm... we got an error message!
		    buffer[rlen] = 0;
		    fprintf(stderr, "%s: %s\n", __func__, buffer+22);

		    if (sscanf(buffer+22, "%s - %d\n", err_type, &err_code) == 2) {
			usbmuxd_send_result(cdata->socket, cdata->tag, err_code);
			return -err_code;
		    } else {
			usbmuxd_send_result(cdata->socket, cdata->tag, ENODATA);
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
    char buffer[1024];
    ssize_t len;
    ssize_t maxlen = sizeof(buffer);
    uint32_t wlen;
    iphone_error_t err;

    if (!arg) {
	fprintf(stderr, "%s: invalid client_data provided!\n", __func__);
	return NULL;
    }

    cdata = (struct client_data*)arg;

    fprintf(stdout, "%s[%d:%d]: started\n", __func__, cdata->duinfo->device_id,cdata->duinfo->use_count);

    if (usbmuxd_handleConnectResult(cdata)) {
	fprintf(stderr, "handleConnectResult: Error\n");
	goto leave;
    }
    fprintf(stdout, "handleConnectResult: Success\n");

    // starting mux reader thread
    cdata->reader_quit = 0;
    cdata->reader_dead = 0;
    if (pthread_create(&cdata->reader, NULL, usbmuxd_client_reader_thread, cdata) != 0) {
	fprintf(stderr, "%s: could not start client_reader thread\n", __func__);
	cdata->reader = 0;
    }

    while (!quit_flag && !cdata->reader_dead) {
	result = check_fd(cdata->socket, FD_READ, DEFAULT_TIMEOUT);
	if (result <= 0) {
	    if (result < 0) {
		fprintf(stderr, "%s: Error: checkfd: %s\n", __func__, strerror(errno));
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
	    fprintf(stderr, "%s[%d:%d]: Error: recv: %s\n", __func__, cdata->duinfo->device_id, cdata->duinfo->use_count, strerror(errno));
	    break;
	}

	cursor = buffer;

	pthread_mutex_lock(&cdata->duinfo->writer_mutex);
	do {
	    wlen = 0;
	    err = iphone_mux_send(cdata->muxclient, cursor, len, &wlen);
	    if (err == IPHONE_E_TIMEOUT) {
		// some kind of timeout... just be patient and retry.
	    } else if (err != IPHONE_E_SUCCESS) {
		fprintf(stderr, "%s[%d:%d]: USB write error: %d\n", __func__, cdata->duinfo->device_id, cdata->duinfo->use_count, err);
		len = -1;
		break;
	    }

	    // calculate remainder.
	    len -= wlen;
	    // advance cursor appropiately.
	    cursor += wlen;
	} while ((len > 0) && !quit_flag);
	pthread_mutex_unlock(&cdata->duinfo->writer_mutex);
	if (len < 0) {
	    break;
	}
    }

leave:
    // cleanup
    fprintf(stdout, "%s[%d:%d]: terminating\n", __func__, cdata->duinfo->device_id, cdata->duinfo->use_count);
    if (cdata->reader != 0) {
	cdata->reader_quit = 1;
	pthread_join(cdata->reader, NULL);
    }

    cdata->handler_dead = 1;

    fprintf(stdout, "%s[%d:%d]: terminated\n", __func__, cdata->duinfo->device_id, cdata->duinfo->use_count);
    return NULL;
}

/**
 * Thread performing usb_bulk_read from the connected device.
 * One thread per device. Lives as long as the device is in use.
 */
static void *usbmuxd_bulk_reader_thread(void *arg)
{
    struct device_use_info *cur_dev;
    
    if (!arg) {
	fprintf(stderr, "%s: Invalid client_data provided\n", __func__);
	return NULL;
    }

    cur_dev = (struct device_use_info*)arg;

    printf("%s: started\n", __func__);

    while (!quit_flag && cur_dev) {
       	
	pthread_mutex_lock(&cur_dev->mutex);
	if (cur_dev->use_count <= 0) {
	    pthread_mutex_unlock(&cur_dev->mutex);
	    break;
	}
	pthread_mutex_unlock(&cur_dev->mutex);

	//pthread_mutex_lock(&cur_dev->reader_mutex);
	iphone_mux_pullbulk(cur_dev->phone);
	//err = iphone_mux_get_error(cdata->muxclient);
	//pthread_mutex_unlock(&cur_dev->reader_mutex);
        //if (err != IPHONE_E_SUCCESS) {
	//    break;
	//}
    }

    printf("%s: terminated\n", __func__);

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
    struct usbmuxd_hello hello;
    struct usbmuxd_device_info_request dev_info_req;
    struct usbmuxd_connect_request c_req;

    struct usb_bus *bus;
    struct usb_device *dev;

    int recv_len;
    int found = 0;    
    int res;
    int i;
//    int sent_result;
//    iphone_error_t err;

    iphone_device_t phone;
    struct device_use_info *cur_dev = NULL;

    if (!arg) {
	fprintf(stderr, "%s: invalid client_data provided!\n", __func__);
	return NULL;
    }

    cdata = (struct client_data*)arg;
    cdata->dead = 0;
    
    fprintf(stdout, "%s: started (fd=%d)\n", __func__, cdata->socket);

    if ((recv_len = usbmuxd_get_request(cdata->socket, &hello, sizeof(hello))) <= 0) {
        fprintf(stderr, "%s: No Hello packet received, error %s\n", __func__, strerror(errno));
	goto leave;
    }

    if ((recv_len == sizeof(hello)) && (hello.header.length == sizeof(hello))
	&& (hello.header.reserved == 0) && (hello.header.type == USBMUXD_HELLO)) {
    	// send success response
	usbmuxd_send_result(cdata->socket, hello.header.tag, 0);
    } else {
	// send error response and exit
        fprintf(stderr, "%s: Invalid Hello packet received.\n", __func__);
	// TODO is this required?!
	usbmuxd_send_result(cdata->socket, hello.header.tag, EINVAL);
	goto leave;
    }

    // gather data about all iPhones/iPods attached
    usb_init();
    usb_find_busses();
    usb_find_devices();

    for (bus = usb_get_busses(); bus; bus = bus->next) {
	for (dev = bus->devices; dev; dev = dev->next) {
	    if (dev->descriptor.idVendor == 0x05ac
		&& dev->descriptor.idProduct >= 0x1290
		&& dev->descriptor.idProduct <= 0x1293)
	    {
		fprintf(stdout, "%s: Found device on bus %d, id %d\n", __func__, bus->location, dev->devnum);
		found++;

		// construct packet
		memset(&dev_info_req, 0, sizeof(dev_info_req));
		dev_info_req.header.length = sizeof(dev_info_req);
		dev_info_req.header.type = USBMUXD_DEVICE_INFO;
		dev_info_req.device_info.device_id = dev->devnum;
		dev_info_req.device_info.product_id = dev->descriptor.idProduct;
		if (dev->descriptor.iSerialNumber) {
		    usb_dev_handle *udev;
		    //pthread_mutex_lock(&usbmux_mutex);
		    udev = usb_open(dev);
		    if (udev) {
			usb_get_string_simple(udev, dev->descriptor.iSerialNumber, dev_info_req.device_info.serial_number, sizeof(dev_info_req.device_info.serial_number)+1);
			usb_close(udev);
		    }
		    //pthread_mutex_unlock(&usbmux_mutex);
		}

#ifdef DEBUG
		print_buffer((char*)&dev_info_req, sizeof(dev_info_req));
#endif

		// send it
		if (send_buf(cdata->socket, &dev_info_req, sizeof(dev_info_req)) <= 0) {
		    fprintf(stderr, "%s: Error: Could not send device info: %s\n", __func__, strerror(errno));
		    found--;
		}
	    }
	}
    }

    // now wait for connect request
    if (found <= 0) {
	fprintf(stderr, "%s: No attached iPhone/iPod devices found.\n", __func__);
	goto leave;
    }

    memset(&c_req, 0, sizeof(c_req));
    if ((recv_len = usbmuxd_get_request(cdata->socket, &c_req, sizeof(c_req))) <= 0) {
	fprintf(stderr, "%s: Did not receive any connect request.\n", __func__);
	goto leave;
    }

    if (c_req.header.type != USBMUXD_CONNECT) {
	fprintf(stderr, "%s: Unexpected packet of type %d received.\n", __func__, c_req.header.type);
	goto leave;
    }

    fprintf(stdout, "%s: Setting up connection to usb device #%d on port %d\n", __func__, c_req.device_id, ntohs(c_req.tcp_dport));

    // find the device, and open usb connection
    phone = NULL;
    cur_dev = NULL;
    // first check if we already have an open connection
    if (device_use_list) {
	pthread_mutex_lock(&usbmux_mutex);
	for (i = 0; i < device_use_count; i++) {
	    if (device_use_list[i]) {
		if (device_use_list[i]->device_id == c_req.device_id) {
		    device_use_list[i]->use_count++;
		    cur_dev = device_use_list[i];
		    phone = cur_dev->phone;
		    break;
		}
	    }
	}
	pthread_mutex_unlock(&usbmux_mutex);
    }
    if (!phone) {
	// if not found, make a new connection
	if (iphone_get_specific_device(0, c_req.device_id, &phone) != IPHONE_E_SUCCESS) {
	    fprintf(stderr, "%s: device_id %d could not be opened\n", __func__, c_req.device_id);
	    usbmuxd_send_result(cdata->socket, c_req.header.tag, ENODEV);
	    goto leave;
	}
	// add to device list
	cur_dev = (struct device_use_info*)malloc(sizeof(struct device_use_info));
	memset(cur_dev, 0, sizeof(struct device_use_info));
	cur_dev->use_count = 1;
	cur_dev->device_id = c_req.device_id;
	cur_dev->phone = phone;
	pthread_mutex_init(&cur_dev->mutex, NULL);
	//pthread_mutex_init(&cur_dev->reader_mutex, NULL);
	pthread_mutex_init(&cur_dev->writer_mutex, NULL);

	fprintf(stdout, "%s: device_use_count = %d\n", __func__, device_use_count);
	pthread_create(&cur_dev->bulk_reader, NULL, usbmuxd_bulk_reader_thread, cur_dev);

	pthread_mutex_lock(&usbmux_mutex);
	device_use_list = (struct device_use_info**)realloc(device_use_list, sizeof(struct device_use_info*) * (device_use_count+1));
	if (device_use_list) {
	    device_use_list[device_use_count] = cur_dev;
	    device_use_count++;
	}
	pthread_mutex_unlock(&usbmux_mutex);
    } else {
	fprintf(stdout, "%s: reusing usb connection device_id %d\n", __func__, c_req.device_id);
    }

    // setup connection to iPhone/iPod
//    pthread_mutex_lock(&usbmux_mutex);
    res = iphone_mux_new_client(cur_dev->phone, 0, ntohs(c_req.tcp_dport), &(cdata->muxclient));
//    pthread_mutex_unlock(&usbmux_mutex);

    if (res != 0) {
	usbmuxd_send_result(cdata->socket, c_req.header.tag, res);
	fprintf(stderr, "%s: mux_new_client returned %d, aborting.\n", __func__, res);
	goto leave;
    }

    // start connection handler thread
    cdata->handler_dead = 0;
    cdata->tag = c_req.header.tag;
    cdata->duinfo = cur_dev;
    if (pthread_create(&cdata->handler, NULL, usbmuxd_client_handler_thread, cdata) != 0) {
	fprintf(stderr, "%s: could not create usbmuxd_client_handler_thread!\n", __func__);
	cdata->handler = 0;
	goto leave;
    }

    //sent_result = 0;

    // TODO: wait for connect result?
    // if connect failed, don't run this loop:

    /*
    // start reading data from the connected device
    while (!quit_flag && !cdata->handler_dead) {
	pthread_mutex_lock(&cur_dev->reader_mutex);
	iphone_mux_pullbulk(cur_dev->phone);
	err = iphone_mux_get_error(cdata->muxclient);
	pthread_mutex_unlock(&cur_dev->reader_mutex);
        if (err != IPHONE_E_SUCCESS) {
	    break;
	}
    }

    if (!sent_result) {
	//fprintf(stderr, "Sending error message %d tag %d\n", err, c_req.header.tag);
	err = iphone_mux_get_error(cdata->muxclient);
	//usbmuxd_send_result(cdata->socket, c_req.header.tag, err); 
    }*/

    //fprintf(stdout, "%s: terminating\n", __func__);

    // wait for handler thread to finish its work
    if (cdata->handler != 0) {
    	pthread_join(cdata->handler, NULL);
    }
    
    fprintf(stdout, "%s: closing connection\n", __func__);

    // time to clean up
    if (cdata && cdata->muxclient) { // should be non-NULL
	iphone_mux_free_client(cdata->muxclient);
    }

leave:
    fprintf(stdout, "%s: terminating\n", __func__);

    // this has to be freed only if it's not in use anymore as it closes
    // the USB connection
    if (cur_dev) {
	pthread_mutex_lock(&cur_dev->mutex);
	if (cur_dev->use_count > 1) {
	    printf("%s: decreasing device use count\n", __func__);
	    cur_dev->use_count--;
	    pthread_mutex_unlock(&cur_dev->mutex);
	} else {
	    printf("%s: last client disconnected, cleaning up\n", __func__);
	    cur_dev->use_count = 0;
	    pthread_mutex_unlock(&cur_dev->mutex);
	    pthread_join(cur_dev->bulk_reader, NULL);
	    iphone_free_device(cur_dev->phone);
	    //pthread_mutex_destroy(&cur_dev->reader_mutex);
	    pthread_mutex_destroy(&cur_dev->writer_mutex);
	    pthread_mutex_destroy(&cur_dev->mutex);
	    free(cur_dev);
	    cur_dev = NULL;
	    pthread_mutex_lock(&usbmux_mutex);
	    if (device_use_count > 1) {
		struct device_use_info **newlist;
		int j;

		newlist = (struct device_use_info**)malloc(sizeof(struct device_use_info*) * device_use_count-1);
		for (i = 0; i < device_use_count; i++) {
		    if (device_use_list[i] != NULL) {
			newlist[j++] = device_use_list[i];
		    }
		}
		free(device_use_list);
		device_use_list = newlist;
	    } else {
		free(device_use_list);
		device_use_list = NULL;
	    }
	    pthread_mutex_unlock(&usbmux_mutex);
	}
    }

    cdata->dead = 1;
    close(cdata->socket);
    
    fprintf(stdout, "%s: terminated\n", __func__);

    return NULL;
}

/**
 * make this program run detached from the current console
 */
static int daemonize()
{
    // TODO still to be implemented, also logging is missing!
    return 0;
}

/**
 * signal handler function for cleaning up properly
 */
static void clean_exit(int sig)
{
    if (sig == SIGINT) {
	fprintf(stdout, "CTRL+C pressed\n");
    }
    quit_flag = 1;
}

/**
 * main function. Initializes all stuff and then loops waiting in accept.
 */
int main(int argc, char **argv)
{
    int foreground = 1;
    struct sockaddr_un c_addr;
    socklen_t len = sizeof(struct sockaddr_un);
    struct client_data *cdata = NULL;
    struct client_data **children = NULL;
    int children_capacity = DEFAULT_CHILDREN_CAPACITY;
    int i = 0;
    int result = 0;
    int cnt = 0;

    fprintf(stdout, "usbmuxd: starting\n");

    // TODO: Parameter checking.

    fsock = create_unix_socket(USBMUXD_SOCKET_FILE);
    if (fsock < 0) {
	fprintf(stderr, "Could not create socket, exiting\n");
	return -1;
    }

    chmod(USBMUXD_SOCKET_FILE, 0666);

    if (!foreground) {
	if (daemonize() < 0) {
	    exit(EXIT_FAILURE);
	}
    }

    // signal(SIGHUP, reload_conf); // none yet
    signal(SIGINT, clean_exit);
    signal(SIGQUIT, clean_exit);
    signal(SIGTERM, clean_exit);
    signal(SIGPIPE, SIG_IGN); 

    // Reserve space for 10 clients which should be enough. If not, the
    // buffer gets enlarged later.
    children = (struct client_data**)malloc(sizeof(struct client_data*) * children_capacity);
    if (!children) {
	fprintf(stderr, "usbmuxd: Out of memory when allocating memory for child threads. Terminating.\n");
	exit(EXIT_FAILURE);
    }
    memset(children, 0, sizeof(struct client_data*) * children_capacity);

    fprintf(stdout, "usbmuxd: waiting for connection\n");
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
			    fprintf(stdout, "usbmuxd: reclaimed client thread (fd=%d)\n", children[i]->socket);
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
			&& ((children_capacity - cnt) <= DEFAULT_CHILDREN_CAPACITY)) {
		    children_capacity = DEFAULT_CHILDREN_CAPACITY;
		    children = realloc(children, sizeof(struct client_data*) * children_capacity);
		}
		continue;
	    } else {
		fprintf(stderr, "usbmuxd: select error: %s\n", strerror(errno));
		continue;
	    }
	}

	cdata = (struct client_data*)malloc(sizeof(struct client_data));
	memset(cdata, 0, sizeof(struct client_data));
	if (!cdata) {
	    quit_flag = 1;
	    fprintf(stderr, "usbmuxd: Error: Out of memory! Terminating.\n");
	    break;
	}

	cdata->socket = accept(fsock, (struct sockaddr*)&c_addr, &len);
       	if (cdata->socket < 0) {
	    free(cdata);
	    if (errno == EINTR) {
		continue;
	    } else {
		fprintf(stderr, "usbmuxd: Error in accept: %s\n", strerror(errno));
		continue;
	    }
	}

	fprintf(stdout, "usbmuxd: new client connected (fd=%d)\n", cdata->socket);

	// create client thread:
	if (pthread_create(&cdata->thread, NULL, usbmuxd_client_init_thread, cdata) == 0) {
	    for (i = 0; i < children_capacity; i++) {
		if (children[i] == NULL) break;
	    }
	    if (i == children_capacity) {
		// enlarge buffer
		children_capacity++;
		children = realloc(children, sizeof(struct client_data*) * children_capacity);
		if (!children) {
		    fprintf(stderr, "usbmuxd: Out of memory when enlarging child thread buffer\n");
		}
	    }
	    children[i] = cdata;
	} else {
	    fprintf(stderr, "usbmuxd: Failed to create client_init_thread.\n");
	    close(cdata->socket);
	    free(cdata);
	    cdata = NULL;
	}
    }

    fprintf(stdout, "usbmuxd: terminating\n");

    // preparing for shutdown: wait for child threads to terminate (if any)
    fprintf(stdout, "usbmuxd: waiting for child threads to terminate...\n");
    for (i = 0; i < children_capacity; i++) {
        if (children[i] != NULL) {
            pthread_join(children[i]->thread, NULL);
	    free(children[i]);
        }
    }

    // delete the children set.
    free(children);
    children = NULL;


    if (fsock >= 0) {
    	close(fsock);
    }

    unlink(USBMUXD_SOCKET_FILE);

    fprintf(stdout, "usbmuxd: terminated\n");

    return 0;
}

