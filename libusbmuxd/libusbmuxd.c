/*
	libusbmuxd - client library to talk to usbmuxd

Copyright (C) 2009-2010	Nikias Bassen <nikias@gmx.li>
Copyright (C) 2009	Paul Sladen <libiphone@paul.sladen.org>
Copyright (C) 2009	Martin Szulecki <opensuse@sukimashita.com>

This library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation, either version 2.1 of the
License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef WIN32
#include <windows.h>
#include <winsock2.h>
#define sleep(x) Sleep(x*1000)
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#ifdef HAVE_PLIST
#include <plist/plist.h>
#define PLIST_BUNDLE_ID "com.marcansoft.usbmuxd"
#define PLIST_CLIENT_VERSION_STRING "usbmuxd built for freedom"
#define PLIST_PROGNAME "libusbmuxd"
#endif

// usbmuxd public interface
#include "usbmuxd.h"
// usbmuxd protocol 
#include "usbmuxd-proto.h"
// socket utility functions
#include "sock_stuff.h"
// misc utility functions
#include "utils.h"

static struct collection devices;
static usbmuxd_event_cb_t event_cb = NULL;
pthread_t devmon;
static int listenfd = -1;

static int use_tag = 0;
static int proto_version = 0;

/**
 * Finds a device info record by its handle.
 * if the record is not found, NULL is returned.
 */
static usbmuxd_device_info_t *devices_find(int handle)
{
	FOREACH(usbmuxd_device_info_t *dev, &devices) {
		if (dev && dev->handle == handle) {
			return dev;
		}
	} ENDFOREACH
	return NULL;
}

/**
 * Creates a socket connection to usbmuxd.
 * For Mac/Linux it is a unix domain socket,
 * for Windows it is a tcp socket.
 */
static int connect_usbmuxd_socket()
{
#if defined(WIN32) || defined(__CYGWIN__)
	return connect_socket("127.0.0.1", USBMUXD_SOCKET_PORT);
#else
	return connect_unix_socket(USBMUXD_SOCKET_FILE);
#endif
}

static int receive_packet(int sfd, struct usbmuxd_header *header, void **payload, int timeout)
{
	int recv_len;
	struct usbmuxd_header hdr;
	char *payload_loc = NULL;

	header->length = 0;
	header->version = 0;
	header->message = 0;
	header->tag = 0;

	recv_len = recv_buf_timeout(sfd, &hdr, sizeof(hdr), 0, timeout);
	if (recv_len < 0) {
		return recv_len;
	} else if (recv_len < sizeof(hdr)) {
		return recv_len;
	}

	uint32_t payload_size = hdr.length - sizeof(hdr);
	if (payload_size > 0) {
		payload_loc = (char*)malloc(payload_size);
		if (recv_buf_timeout(sfd, payload_loc, payload_size, 0, 5000) != payload_size) {
			fprintf(stderr, "%s: Error receiving payload of size %d\n", __func__, payload_size);
			free(payload_loc);
			return -EBADMSG;
		}
	}

#ifdef HAVE_PLIST
	if (hdr.message == MESSAGE_PLIST) {
		char *message = NULL;
		plist_t plist = NULL;
		plist_from_xml(payload_loc, payload_size, &plist);
		free(payload_loc);

		if (!plist) {
			fprintf(stderr, "%s: Error getting plist from payload!\n", __func__);
			return -EBADMSG;
		}

		plist_t node = plist_dict_get_item(plist, "MessageType");
		if (plist_get_node_type(node) != PLIST_STRING) {
			fprintf(stderr, "%s: Error getting message type from plist!\n", __func__);
			free(plist);
			return -EBADMSG;
		}
		
		plist_get_string_val(node, &message);
		if (message) {
			uint64_t val = 0;
			if (strcmp(message, "Result") == 0) {
				/* result message */
				uint32_t dwval = 0;
				plist_t n = plist_dict_get_item(plist, "Number");
				plist_get_uint_val(n, &val);
				*payload = malloc(sizeof(uint32_t));
				dwval = val;
				memcpy(*payload, &dwval, sizeof(dwval));
				hdr.length = sizeof(hdr) + sizeof(dwval);
				hdr.message = MESSAGE_RESULT;
			} else if (strcmp(message, "Attached") == 0) {
				/* device add message */
				struct usbmuxd_device_record *dev = NULL;
				plist_t props = plist_dict_get_item(plist, "Properties");
				if (!props) {
					fprintf(stderr, "%s: Could not get properties for message '%s' from plist!\n", __func__, message);
					plist_free(plist);
					return -EBADMSG;
				}
				dev = (struct usbmuxd_device_record*)malloc(sizeof(struct usbmuxd_device_record));
				memset(dev, 0, sizeof(struct usbmuxd_device_record));

				plist_t n = plist_dict_get_item(props, "DeviceID");
				plist_get_uint_val(n, &val);
				dev->device_id = (uint32_t)val;

				n = plist_dict_get_item(props, "ProductID");
				plist_get_uint_val(n, &val);
				dev->product_id = (uint32_t)val;

				n = plist_dict_get_item(props, "SerialNumber");
				char *strval = NULL;
				plist_get_string_val(n, &strval);
				if (strval) {
					strcpy(dev->serial_number, strval);
					free(strval);
				}
				n = plist_dict_get_item(props, "LocationID");
				plist_get_uint_val(n, &val);
				dev->location = (uint32_t)val;
				*payload = (void*)dev;
				hdr.length = sizeof(hdr) + sizeof(struct usbmuxd_device_record);
				hdr.message = MESSAGE_DEVICE_ADD;
			} else if (strcmp(message, "Detached") == 0) {
				/* device remove message */
				uint32_t dwval = 0;
				plist_t n = plist_dict_get_item(plist, "DeviceID");
				if (n) {
					plist_get_uint_val(n, &val);
					*payload = malloc(sizeof(uint32_t));
					dwval = val;
					memcpy(*payload, &dwval, sizeof(dwval));
					hdr.length = sizeof(hdr) + sizeof(dwval);
					hdr.message = MESSAGE_DEVICE_REMOVE;
				}
			} else {
				fprintf(stderr, "%s: Unexpected message '%s' in plist!\n", __func__, message);
				plist_free(plist);
				return -EBADMSG;
			}
		}
		plist_free(plist);
	} else
#endif
	{
		*payload = payload_loc;
	}

	memcpy(header, &hdr, sizeof(hdr));

	return hdr.length;
}

/**
 * Retrieves the result code to a previously sent request.
 */
static int usbmuxd_get_result(int sfd, uint32_t tag, uint32_t * result)
{
	struct usbmuxd_header hdr;
	int recv_len;
	uint32_t *res = NULL;

	if (!result) {
		return -EINVAL;
	}
	*result = -1;

	if ((recv_len = receive_packet(sfd, &hdr, (void**)&res, 5000)) < 0) {
		fprintf(stderr, "%s: Error receiving packet: %d\n", __func__, errno);
		if (res)
			free(res);
		return -errno;
	}
	if (recv_len < sizeof(hdr)) {
		fprintf(stderr, "%s: Received packet is too small!\n", __func__);
		if (res)
			free(res);
		return -EPROTO;
	}

	if (hdr.message == MESSAGE_RESULT) {
		int ret = 0;
		if (res && (hdr.tag == tag)) {
			memcpy(result, res, sizeof(uint32_t));
			ret = 1;
		}
		if (res)
			free(res);
		return ret;
	}
	fprintf(stderr, "%s: Unexpected message of type %d received!\n", __func__, hdr.message);
	if (res)
		free(res);
	return -EPROTO;
}

static int send_packet(int sfd, uint32_t message, uint32_t tag, void *payload, uint32_t payload_size)
{
	struct usbmuxd_header header;

	header.length = sizeof(struct usbmuxd_header);
	header.version = proto_version;
	header.message = message;
	header.tag = tag;
	if (payload && (payload_size > 0)) {
		header.length += payload_size;
	}
	int sent = send_buf(sfd, &header, sizeof(header));
	if (sent != sizeof(header)) {
		fprintf(stderr, "%s: ERROR: could not send packet header\n", __func__);
		return -1;
	}
	if (payload && (payload_size > 0)) {
		sent += send_buf(sfd, payload, payload_size);
	}
	if (sent != (int)header.length) {
		fprintf(stderr, "%s: ERROR: could not send whole packet\n", __func__);
		close_socket(sfd);
		return -1;
	}
	return sent;
}

static int send_listen_packet(int sfd, uint32_t tag)
{
	int res = 0;
#ifdef HAVE_PLIST
	if (proto_version == 1) {
		/* plist packet */
		char *payload = NULL;
		uint32_t payload_size = 0;
		plist_t plist;

		/* construct message plist */
		plist = plist_new_dict();
		plist_dict_insert_item(plist, "BundleID", plist_new_string(PLIST_BUNDLE_ID));
		plist_dict_insert_item(plist, "ClientVersionString", plist_new_string(PLIST_CLIENT_VERSION_STRING));
		plist_dict_insert_item(plist, "MessageType", plist_new_string("Listen"));
		plist_dict_insert_item(plist, "ProgName", plist_new_string(PLIST_PROGNAME));
		plist_to_xml(plist, &payload, &payload_size);
		plist_free(plist);

		res = send_packet(sfd, MESSAGE_PLIST, tag, payload, payload_size);
		free(payload);
	} else
#endif
	{
		/* binary packet */
		res = send_packet(sfd, MESSAGE_LISTEN, tag, NULL, 0);
	}
	return res;
}

static int send_connect_packet(int sfd, uint32_t tag, uint32_t device_id, uint16_t port)
{
	int res = 0;
#ifdef HAVE_PLIST
	if (proto_version == 1) {
		/* plist packet */
		char *payload = NULL;
		uint32_t payload_size = 0;
		plist_t plist;

		/* construct message plist */
		plist = plist_new_dict();
		plist_dict_insert_item(plist, "BundleID", plist_new_string(PLIST_BUNDLE_ID));
		plist_dict_insert_item(plist, "ClientVersionString", plist_new_string(PLIST_CLIENT_VERSION_STRING));
		plist_dict_insert_item(plist, "MessageType", plist_new_string("Connect"));
		plist_dict_insert_item(plist, "DeviceID", plist_new_uint(device_id));
		plist_dict_insert_item(plist, "PortNumber", plist_new_uint(htons(port)));
		plist_dict_insert_item(plist, "ProgName", plist_new_string(PLIST_PROGNAME));
		plist_to_xml(plist, &payload, &payload_size);
		plist_free(plist);

		res = send_packet(sfd, MESSAGE_PLIST, tag, (void*)payload, payload_size);
		free(payload);
	} else
#endif
	{
		/* binary packet */
		struct {
			uint32_t device_id;
			uint16_t port;
			uint16_t reserved;
		} conninfo;

		conninfo.device_id = device_id;
		conninfo.port = htons(port);
		conninfo.reserved = 0;

		res = send_packet(sfd, MESSAGE_CONNECT, tag, &conninfo, sizeof(conninfo));
	}
	return res;
}

/**
 * Generates an event, i.e. calls the callback function.
 * A reference to a populated usbmuxd_event_t with information about the event
 * and the corresponding device will be passed to the callback function.
 */
static void generate_event(usbmuxd_event_cb_t callback, const usbmuxd_device_info_t *dev, enum usbmuxd_event_type event, void *user_data)
{
	usbmuxd_event_t ev;

	if (!callback || !dev) {
		return;
	}

	ev.event = event;
	memcpy(&ev.device, dev, sizeof(usbmuxd_device_info_t));

	callback(&ev, user_data);
}

/**
 * Tries to connect to usbmuxd and wait if it is not running.
 * 
 * TODO inotify support should come here
 */
static int usbmuxd_listen()
{
	int sfd;
	uint32_t res = -1;

#ifdef HAVE_PLIST
retry:
#endif
	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		while (event_cb) {
			if ((sfd = connect_usbmuxd_socket()) > 0) {
				break;
			}
			sleep(1);
		}
	}

	if (sfd < 0) {
		fprintf(stderr, "%s: ERROR: usbmuxd was supposed to be running here...\n", __func__);
		return sfd;
	}

	use_tag++;
	if (send_listen_packet(sfd, use_tag) <= 0) {
		fprintf(stderr, "%s: ERROR: could not send listen packet\n", __func__);
		close_socket(sfd);
		return -1;
	}
	if (usbmuxd_get_result(sfd, use_tag, &res) && (res != 0)) {
		close_socket(sfd);
#ifdef HAVE_PLIST
		if ((res == RESULT_BADVERSION) && (proto_version != 1)) {
			proto_version = 1;
			goto retry;
		}
#endif
		fprintf(stderr, "%s: ERROR: did not get OK but %d\n", __func__, res);
		return -1;
	}

	return sfd;
}

/**
 * Waits for an event to occur, i.e. a packet coming from usbmuxd.
 * Calls generate_event to pass the event via callback to the client program.
 */
int get_next_event(int sfd, usbmuxd_event_cb_t callback, void *user_data)
{
	struct usbmuxd_header hdr;
	void *payload = NULL;

	/* block until we receive something */
	if (receive_packet(sfd, &hdr, &payload, 0) < 0) {
		// when then usbmuxd connection fails,
		// generate remove events for every device that
		// is still present so applications know about it
		FOREACH(usbmuxd_device_info_t *dev, &devices) {
			generate_event(callback, dev, UE_DEVICE_REMOVE, user_data);
			collection_remove(&devices, dev);
		} ENDFOREACH
		return -EIO;
	}

	if ((hdr.length > sizeof(hdr)) && !payload) {
		fprintf(stderr, "%s: Invalid packet received, payload is missing!\n", __func__);
		return -EBADMSG;
	}

	if (hdr.message == MESSAGE_DEVICE_ADD) {
		struct usbmuxd_device_record *dev = payload;
		usbmuxd_device_info_t *devinfo = (usbmuxd_device_info_t*)malloc(sizeof(usbmuxd_device_info_t));
		if (!devinfo) {
			fprintf(stderr, "%s: Out of memory!\n", __func__);
			free(payload);
			return -1;
		}

		devinfo->handle = dev->device_id;
		devinfo->product_id = dev->product_id;
		memset(devinfo->uuid, '\0', sizeof(devinfo->uuid));
		memcpy(devinfo->uuid, dev->serial_number, sizeof(devinfo->uuid));

		collection_add(&devices, devinfo);
		generate_event(callback, devinfo, UE_DEVICE_ADD, user_data);
	} else if (hdr.message == MESSAGE_DEVICE_REMOVE) {
		uint32_t handle;
		usbmuxd_device_info_t *devinfo;

		memcpy(&handle, payload, sizeof(uint32_t));

		devinfo = devices_find(handle);
		if (!devinfo) {
			fprintf(stderr, "%s: WARNING: got device remove message for handle %d, but couldn't find the corresponding handle in the device list. This event will be ignored.\n", __func__, handle);
		} else {
			generate_event(callback, devinfo, UE_DEVICE_REMOVE, user_data);
			collection_remove(&devices, devinfo);
		}
	} else {
		fprintf(stderr, "%s: Unexpected message type %d length %d received!\n", __func__, hdr.message, hdr.length);
	}
	if (payload) {
		free(payload);
	}
	return 0;
}

/**
 * Device Monitor thread function.
 *
 * This function sets up a connection to usbmuxd
 */
static void *device_monitor(void *data)
{
	collection_init(&devices);

	while (event_cb) {

		listenfd = usbmuxd_listen();
		if (listenfd < 0) {
			continue;
		}

		while (event_cb) {
			int res = get_next_event(listenfd, event_cb, data);
			if (res < 0) {
			    break;
			}
		}
	}

	collection_free(&devices);

	return NULL;
}

int usbmuxd_subscribe(usbmuxd_event_cb_t callback, void *user_data)
{
	int res;

	if (!callback) {
		return -EINVAL;
	}
	event_cb = callback;

	res = pthread_create(&devmon, NULL, device_monitor, user_data);
	if (res != 0) {
		fprintf(stderr, "%s: ERROR: Could not start device watcher thread!\n", __func__);
		return res;
	}
	return 0;
}

int usbmuxd_unsubscribe()
{
	event_cb = NULL;

	if (pthread_kill(devmon, 0) == 0) {
		close_socket(listenfd);
		listenfd = -1;
		pthread_kill(devmon, SIGINT);
		pthread_join(devmon, NULL);
	}

	return 0;
}

int usbmuxd_get_device_list(usbmuxd_device_info_t **device_list)
{
	int sfd;
	int listen_success = 0;
	uint32_t res;
	usbmuxd_device_info_t *newlist = NULL;
	struct usbmuxd_header hdr;
	struct usbmuxd_device_record *dev_info;
	int dev_cnt = 0;
	void *payload = NULL;

#ifdef HAVE_PLIST
retry:
#endif
	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		fprintf(stderr, "%s: error opening socket!\n", __func__);
		return sfd;
	}

	use_tag++;
	if (send_listen_packet(sfd, use_tag) > 0) {
		res = -1;
		// get response
		if (usbmuxd_get_result(sfd, use_tag, &res) && (res == 0)) {
			listen_success = 1;
		} else {
			close_socket(sfd);
#ifdef HAVE_PLIST
			if ((res == RESULT_BADVERSION) && (proto_version != 1)) {
				proto_version = 1;
				goto retry;
			}
#endif
			fprintf(stderr,
					"%s: Did not get response to scan request (with result=0)...\n",
					__func__);
			return res;
		}
	}

	if (!listen_success) {
		fprintf(stderr, "%s: Could not send listen request!\n", __func__);
		return -1;
	}

	*device_list = NULL;
	// receive device list
	while (1) {
		if (receive_packet(sfd, &hdr, &payload, 1000) > 0) {
			if (hdr.message == MESSAGE_DEVICE_ADD) {
				dev_info = payload;
				newlist = (usbmuxd_device_info_t *) realloc(*device_list, sizeof(usbmuxd_device_info_t) * (dev_cnt + 1));
				if (newlist) {
					newlist[dev_cnt].handle =
						(int) dev_info->device_id;
					newlist[dev_cnt].product_id =
						dev_info->product_id;
					memset(newlist[dev_cnt].uuid, '\0',
						   sizeof(newlist[dev_cnt].uuid));
					memcpy(newlist[dev_cnt].uuid,
						   dev_info->serial_number,
						   sizeof(newlist[dev_cnt].uuid));
					*device_list = newlist;
					dev_cnt++;
				} else {
					fprintf(stderr,
						"%s: ERROR: out of memory when trying to realloc!\n",
						__func__);
					if (payload)
						free(payload);
					break;
				}
			} else {
				fprintf(stderr, "%s: Unexpected message %d\n", __func__, hdr.message);
			}
			if (payload)
				free(payload);
		} else {
			// we _should_ have all of them now.
			// or perhaps an error occured.
			break;
		}
	}

	// explicitly close connection
	close_socket(sfd);

	// terminating zero record
	newlist = (usbmuxd_device_info_t*) realloc(*device_list, sizeof(usbmuxd_device_info_t) * (dev_cnt + 1));
	memset(newlist + dev_cnt, 0, sizeof(usbmuxd_device_info_t));
	*device_list = newlist;

	return dev_cnt;
}

int usbmuxd_device_list_free(usbmuxd_device_info_t **device_list)
{
	if (device_list) {
		free(*device_list);
	}
	return 0;
}

int usbmuxd_get_device_by_uuid(const char *uuid, usbmuxd_device_info_t *device)
{
	usbmuxd_device_info_t *dev_list = NULL;

	if (!device) {
		return -EINVAL;
	}
	if (usbmuxd_get_device_list(&dev_list) < 0) {
		return -ENODEV;
	}

	int i;
	int result = 0;
	for (i = 0; dev_list[i].handle > 0; i++) {
	 	if (!uuid) {
			device->handle = dev_list[i].handle;
			device->product_id = dev_list[i].product_id;
			strcpy(device->uuid, dev_list[i].uuid);
			result = 1;
			break;
		}
		if (!strcmp(uuid, dev_list[i].uuid)) {
			device->handle = dev_list[i].handle;
			device->product_id = dev_list[i].product_id;
			strcpy(device->uuid, dev_list[i].uuid);
			result = 1;
			break;
		}
	}

	free(dev_list);

	return result;
}

int usbmuxd_connect(const int handle, const unsigned short port)
{
	int sfd;
	int connected = 0;
	uint32_t res = -1;

#ifdef HAVE_PLIST
retry:
#endif
	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		fprintf(stderr, "%s: Error: Connection to usbmuxd failed: %s\n",
				__func__, strerror(errno));
		return sfd;
	}

	use_tag++;
	if (send_connect_packet(sfd, use_tag, (uint32_t)handle, (uint16_t)port) <= 0) {
		fprintf(stderr, "%s: Error sending connect message!\n", __func__);
	} else {
		// read ACK
		//fprintf(stderr, "%s: Reading connect result...\n", __func__);
		if (usbmuxd_get_result(sfd, use_tag, &res)) {
			if (res == 0) {
				//fprintf(stderr, "%s: Connect success!\n", __func__);
				connected = 1;
			} else {
#ifdef HAVE_PLIST
				if ((res == RESULT_BADVERSION) && (proto_version == 0)) {
					proto_version = 1;
					close_socket(sfd);
					goto retry;
				}
#endif
				fprintf(stderr, "%s: Connect failed, Error code=%d\n",
						__func__, res);
			}
		}
	}

	if (connected) {
		return sfd;
	}

	close_socket(sfd);

	return -1;
}

int usbmuxd_disconnect(int sfd)
{
	return close_socket(sfd);
}

int usbmuxd_send(int sfd, const char *data, uint32_t len, uint32_t *sent_bytes)
{
	int num_sent;

	if (sfd < 0) {
		return -EINVAL;
	}
	
	num_sent = send(sfd, (void*)data, len, 0);
	if (num_sent < 0) {
		*sent_bytes = 0;
		fprintf(stderr, "%s: Error %d when sending: %s\n", __func__, num_sent, strerror(errno));
		return num_sent;
	} else if ((uint32_t)num_sent < len) {
		fprintf(stderr, "%s: Warning: Did not send enough (only %d of %d)\n", __func__, num_sent, len);
	}

	*sent_bytes = num_sent;

	return 0;
}

int usbmuxd_recv_timeout(int sfd, char *data, uint32_t len, uint32_t *recv_bytes, unsigned int timeout)
{
	int num_recv = recv_buf_timeout(sfd, (void*)data, len, 0, timeout);
	if (num_recv < 0) {
		*recv_bytes = 0;
		return num_recv;
	}

	*recv_bytes = num_recv;

	return 0;
}

int usbmuxd_recv(int sfd, char *data, uint32_t len, uint32_t *recv_bytes)
{
	return usbmuxd_recv_timeout(sfd, data, len, recv_bytes, 5000);
}

