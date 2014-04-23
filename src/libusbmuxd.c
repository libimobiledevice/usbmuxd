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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef WIN32
	#include <winsock2.h>
	
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	
	#define strcasecmp _stricmp
	#define sleep(x) Sleep(x*1000)
	#ifndef EPROTO
		#define EPROTO 134
	#endif
	#ifndef EBADMSG
		#define EBADMSG 104
	#endif

	#ifdef _MSC_VER
		#define __func__ __FUNCTION__
	#endif
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#endif

#ifdef HAVE_INOTIFY
#include <sys/inotify.h>
#define EVENT_SIZE  (sizeof (struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))
#define USBMUXD_DIRNAME "/var/run"
#define USBMUXD_SOCKET_NAME "usbmuxd"
#endif /* HAVE_INOTIFY */

#ifndef _MSC_VER
	#include <unistd.h>
#endif

#include <signal.h>

#include <plist/plist.h>
#define PLIST_BUNDLE_ID "org.libimobiledevice.usbmuxd"
#define PLIST_CLIENT_VERSION_STRING "usbmuxd built for freedom"
#define PLIST_PROGNAME "libusbmuxd"
#define PLIST_LIBUSBMUX_VERSION 3

// usbmuxd public interface
#include "usbmuxd.h"
// usbmuxd protocol
#include "usbmuxd-proto.h"
// socket utility functions
#include "socket.h"
// misc utility functions
#include "collection.h"

static int libusbmuxd_debug = 0;
#define DEBUG(x, y, ...) if (x <= libusbmuxd_debug) fprintf(stderr, (y), __VA_ARGS__); fflush(stderr);

static struct collection devices;
static usbmuxd_event_cb_t event_cb = NULL;
#ifdef WIN32
HANDLE devmon = NULL;
#else
pthread_t devmon;
#endif
static int listenfd = -1;

static volatile int use_tag = 0;
static volatile int proto_version = 1;
static volatile int try_list_devices = 1;

static int usbmuxd_port = DEFAULT_USBMUXD_SOCKET_PORT;

/**
 * Finds a device info record by its handle.
 * if the record is not found, NULL is returned.
 */
static usbmuxd_device_info_t *devices_find(uint32_t handle)
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
	return socket_connect("127.0.0.1", usbmuxd_port);
#else
	return socket_connect_unix(USBMUXD_SOCKET_FILE);
#endif
}

static struct usbmuxd_device_record* device_record_from_plist(plist_t props)
{
	struct usbmuxd_device_record* dev = NULL;
	plist_t n = NULL;
	uint64_t val = 0;
	char *strval = NULL;

	dev = (struct usbmuxd_device_record*)malloc(sizeof(struct usbmuxd_device_record));
	if (!dev)
		return NULL;
	memset(dev, 0, sizeof(struct usbmuxd_device_record));

	n = plist_dict_get_item(props, "DeviceID");
	if (n && plist_get_node_type(n) == PLIST_UINT) {
		plist_get_uint_val(n, &val);
		dev->device_id = (uint32_t)val;
	}

	n = plist_dict_get_item(props, "ProductID");
	if (n && plist_get_node_type(n) == PLIST_UINT) {
		plist_get_uint_val(n, &val);
		dev->product_id = (uint32_t)val;
	}

	n = plist_dict_get_item(props, "SerialNumber");
	if (n && plist_get_node_type(n) == PLIST_STRING) {
		plist_get_string_val(n, &strval);
		if (strval) {
			strncpy(dev->serial_number, strval, 255);
			plist_free_memory(strval);
		}
	}
	n = plist_dict_get_item(props, "LocationID");
	if (n && plist_get_node_type(n) == PLIST_UINT) {
		plist_get_uint_val(n, &val);
		dev->location = (uint32_t)val;
	}

	return dev;
}

static int parse_packet_with_dev_info(plist_t plist, uint32_t message, struct usbmuxd_header * hdr, void **payload)
{
	/* device add\trust message */
	struct usbmuxd_device_record *dev = NULL;
	plist_t props = plist_dict_get_item(plist, "Properties");
	if (!props) {
		DEBUG(1, "%s: Could not get properties for message '%d' from plist!\n", __func__, message);
		return -EBADMSG;
	}

	dev = device_record_from_plist(props);
	if (!dev) {
		DEBUG(1, "%s: Could not create device record object from properties!\n", __func__);
		return -EBADMSG;
	}

	*payload = (void*)dev;
	hdr->length = sizeof(*hdr) + sizeof(struct usbmuxd_device_record);
	hdr->message = message;

	return 0;
}

static int parse_result_packet(plist_t plist, struct usbmuxd_header * hdr, void **payload)
{
	uint64_t val = 0;
	uint32_t dwval = 0;
	plist_t n = plist_dict_get_item(plist, "Number");
	plist_get_uint_val(n, &val);
	*payload = malloc(sizeof(uint32_t));
	dwval = val;
	memcpy(*payload, &dwval, sizeof(dwval));
	hdr->length = sizeof(*hdr)+sizeof(dwval);
	hdr->message = MESSAGE_RESULT;

	return 0;
}

static int parse_device_remove_packet(plist_t plist, struct usbmuxd_header * hdr, void **payload)
{
	uint64_t val = 0;
	uint32_t dwval = 0;
	plist_t n = plist_dict_get_item(plist, "DeviceID");
	if (n) {
		plist_get_uint_val(n, &val);
		*payload = malloc(sizeof(uint32_t));
		dwval = val;
		memcpy(*payload, &dwval, sizeof(dwval));
		hdr->length = sizeof(*hdr) + sizeof(dwval);
		hdr->message = MESSAGE_DEVICE_REMOVE;
	}

	return 0;
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

	recv_len = socket_receive_timeout(sfd, &hdr, sizeof(hdr), 0, timeout);
	if (recv_len < 0) {
		return recv_len;
	} else if ((size_t)recv_len < sizeof(hdr)) {
		return recv_len;
	}

	uint32_t payload_size = hdr.length - sizeof(hdr);
	if (payload_size > 0) {
		payload_loc = (char*)malloc(payload_size);
		uint32_t rsize = 0;
		do {
			int res = socket_receive_timeout(sfd, payload_loc + rsize, payload_size - rsize, 0, 5000);
			if (res < 0) {
				break;
			}
			rsize += res;
		} while (rsize < payload_size);
		if (rsize != payload_size) {
			DEBUG(1, "%s: Error receiving payload of size %d (bytes received: %d)\n", __func__, payload_size, rsize);
			free(payload_loc);
			return -EBADMSG;
		}
	}

	if (hdr.message == MESSAGE_PLIST) {
		char *message = NULL;
		plist_t plist = NULL;
		plist_from_xml(payload_loc, payload_size, &plist);
		free(payload_loc);

		if (!plist) {
			DEBUG(1, "%s: Error getting plist from payload!\n", __func__);
			return -EBADMSG;
		}

		plist_t node = plist_dict_get_item(plist, "MessageType");
		if (!node || plist_get_node_type(node) != PLIST_STRING) {
			*payload = plist;
			hdr.length = sizeof(hdr);
			memcpy(header, &hdr, sizeof(hdr));
			return hdr.length;
		}

		plist_get_string_val(node, &message);
		if (message) {
			int ret = 0;

			/* Result Message */
			if (strcmp(message, "Result") == 0) {
				ret = parse_result_packet(plist, &hdr, payload);
			/* Device Attached */
			} else if (strcmp(message, "Attached") == 0) {
				ret = parse_packet_with_dev_info(plist, MESSAGE_DEVICE_ADD, &hdr, payload);
			/* Trust dialog is pending */
			} else if (strcmp(message, "TrustPending") == 0) {
				ret = parse_packet_with_dev_info(plist, MESSAGE_DEVICE_TRUST_PENDING, &hdr, payload);
			/* Device is password protected */
			} else if (strcmp(message, "PasswordProtected") == 0) {
				ret = parse_packet_with_dev_info(plist, MESSAGE_DEVICE_PASSWORD_PROTECTED, &hdr, payload);
			/* User has denied pairing */
			} else if (strcmp(message, "UserDeniedPairing") == 0) {
				ret = parse_packet_with_dev_info(plist, MESSAGE_DEVICE_USER_DENIED_PAIRING, &hdr, payload);
			/* Device Detached */
			} else if (strcmp(message, "Detached") == 0) {
				ret = parse_device_remove_packet(plist, &hdr, payload);
			/* Unknown Message */
			} else {
				DEBUG(1, "%s: Unexpected message '%s' in plist!\n", __func__, message);
				ret = -EBADMSG;
			}

			plist_free_memory(message);
			if (ret < 0) {
				plist_free(plist);
				return ret;
			}
		}
		plist_free(plist);
	} else {
		*payload = payload_loc;
	}

	memcpy(header, &hdr, sizeof(hdr));

	return hdr.length;
}

/**
 * Retrieves the result code to a previously sent request.
 */
static int usbmuxd_get_result(int sfd, uint32_t tag, uint32_t *result, void **result_plist)
{
	struct usbmuxd_header hdr;
	int recv_len;
	uint32_t *res = NULL;

	if (!result) {
		return -EINVAL;
	}
	*result = -1;
	if (result_plist) {
		*result_plist = NULL;
	}

	if ((recv_len = receive_packet(sfd, &hdr, (void**)&res, 5000)) < 0) {
		DEBUG(1, "%s: Error receiving packet: %d\n", __func__, recv_len);
		if (res)
			free(res);
		return recv_len;
	}
	if ((size_t)recv_len < sizeof(hdr)) {
		DEBUG(1, "%s: Received packet is too small!\n", __func__);
		if (res)
			free(res);
		return -EPROTO;
	}

	if (hdr.message == MESSAGE_RESULT) {
		int ret = 0;
		if (hdr.tag != tag) {
			DEBUG(1, "%s: WARNING: tag mismatch (%d != %d). Proceeding anyway.\n", __func__, hdr.tag, tag);
		}
		if (res) {
			memcpy(result, res, sizeof(uint32_t));
			ret = 1;
		}
		if (res)
			free(res);
		return ret;
	} else if (hdr.message == MESSAGE_PLIST) {
		if (!result_plist) {
			DEBUG(1, "%s: MESSAGE_PLIST result but result_plist pointer is NULL!\n", __func__);
			return -1;
		}
		*result_plist = (plist_t)res;
		*result = RESULT_OK;
		return 1;
	}

	DEBUG(1, "%s: Unexpected message of type %d received!\n", __func__, hdr.message);
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
	int sent = socket_send(sfd, &header, sizeof(header));
	if (sent != sizeof(header)) {
		DEBUG(1, "%s: ERROR: could not send packet header\n", __func__);
		return -1;
	}
	if (payload && (payload_size > 0)) {
		uint32_t ssize = 0;
		do {
			int res = socket_send(sfd, (char*)payload + ssize, payload_size - ssize);
			if (res < 0) {
				break;
			}
			ssize += res;
		} while (ssize < payload_size);
		sent += ssize;
	}
	if (sent != (int)header.length) {
		DEBUG(1, "%s: ERROR: could not send whole packet (sent %d of %d)\n", __func__, sent, header.length);
		socket_close(sfd);
		return -1;
	}
	return sent;
}

static int send_plist_packet(int sfd, uint32_t tag, plist_t message)
{
	int res;
	char *payload = NULL;
	uint32_t payload_size = 0;

	plist_to_xml(message, &payload, &payload_size);
	res = send_packet(sfd, MESSAGE_PLIST, tag, payload, payload_size);
	plist_free_memory(payload);

	return res;
}

static plist_t create_plist_message(const char* message_type)
{
	plist_t plist = plist_new_dict();
	plist_dict_set_item(plist, "BundleID", plist_new_string(PLIST_BUNDLE_ID));
	plist_dict_set_item(plist, "ClientVersionString", plist_new_string(PLIST_CLIENT_VERSION_STRING));
	plist_dict_set_item(plist, "MessageType", plist_new_string(message_type));
	plist_dict_set_item(plist, "ProgName", plist_new_string(PLIST_PROGNAME));	
	plist_dict_set_item(plist, "kLibUSBMuxVersion", plist_new_uint(PLIST_LIBUSBMUX_VERSION));
	return plist;
}

static int send_listen_packet(int sfd, uint32_t tag)
{
	int res = 0;
	if (proto_version == 1) {
		/* construct message plist */
		plist_t plist = create_plist_message("Listen");

		res = send_plist_packet(sfd, tag, plist);
		plist_free(plist);
	} else {
		/* binary packet */
		res = send_packet(sfd, MESSAGE_LISTEN, tag, NULL, 0);
	}
	return res;
}

static int send_connect_packet(int sfd, uint32_t tag, uint32_t device_id, uint16_t port)
{
	int res = 0;
	if (proto_version == 1) {
		/* construct message plist */
		plist_t plist = create_plist_message("Connect");
		plist_dict_set_item(plist, "DeviceID", plist_new_uint(device_id));
		plist_dict_set_item(plist, "PortNumber", plist_new_uint(htons(port)));

		res = send_plist_packet(sfd, tag, plist);
		plist_free(plist);
	} else {
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

static int send_list_devices_packet(int sfd, uint32_t tag)
{
	int res = -1;

	/* construct message plist */
	plist_t plist = create_plist_message("ListDevices");

	res = send_plist_packet(sfd, tag, plist);
	plist_free(plist);

	return res;
}

static int send_read_buid_packet(int sfd, uint32_t tag)
{
	int res = -1;

	/* construct message plist */
	plist_t plist = create_plist_message("ReadBUID");

	res = send_plist_packet(sfd, tag, plist);
	plist_free(plist);

	return res;
}

static int send_pair_record_packet(int sfd, uint32_t tag, const char* msgtype, const char* pair_record_id, plist_t data)
{
	int res = -1;

	/* construct message plist */
	plist_t plist = create_plist_message(msgtype);
	plist_dict_set_item(plist, "PairRecordID", plist_new_string(pair_record_id));
	if (data) {
		plist_dict_set_item(plist, "PairRecordData", plist_copy(data));
	}
	
	res = send_plist_packet(sfd, tag, plist);
	plist_free(plist);

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

static int usbmuxd_listen_poll()
{
	int sfd;

	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		while (event_cb) {
			if ((sfd = connect_usbmuxd_socket()) > 0) {
				break;
			}
			sleep(1);
		}
	}

	return sfd;
}

#ifdef HAVE_INOTIFY
static int use_inotify = 1;

static int usbmuxd_listen_inotify()
{
	int inot_fd;
	int watch_d;
	int sfd;

	if (!use_inotify) {
		return -2;
	}

	sfd = connect_usbmuxd_socket();
	if (sfd >= 0)
		return sfd;

	sfd = -1;
	inot_fd = inotify_init ();
	if (inot_fd < 0) {
		DEBUG(1, "%s: Failed to setup inotify\n", __func__);
		return -2;
	}

	/* inotify is setup, listen for events that concern us */
	watch_d = inotify_add_watch (inot_fd, USBMUXD_DIRNAME, IN_CREATE);
	if (watch_d < 0) {
		DEBUG(1, "%s: Failed to setup watch descriptor for socket dir\n", __func__);
		close (inot_fd);
		return -2;
	}

	while (1) {
		ssize_t len, i;
		char buff[EVENT_BUF_LEN] = {0};

		i = 0;
		len = read (inot_fd, buff, EVENT_BUF_LEN -1);
		if (len < 0)
			goto end;
		while (i < len) {
			struct inotify_event *pevent = (struct inotify_event *) & buff[i];

			/* check that it's ours */
			if (pevent->mask & IN_CREATE &&
			    pevent->len &&
			    pevent->name != NULL &&
			    strcmp(pevent->name, USBMUXD_SOCKET_NAME) == 0) {
				sfd = connect_usbmuxd_socket ();
				goto end;
			}
			i += EVENT_SIZE + pevent->len;
		}
	}

end:
	inotify_rm_watch(inot_fd, watch_d);
	close(inot_fd);

	return sfd;
}
#endif /* HAVE_INOTIFY */

/**
 * Tries to connect to usbmuxd and wait if it is not running.
 */
static int usbmuxd_listen()
{
	int sfd;
	uint32_t res = -1;
	int tag;

retry:

#ifdef HAVE_INOTIFY
	sfd = usbmuxd_listen_inotify();
	if (sfd == -2)
		sfd = usbmuxd_listen_poll();
#else
	sfd = usbmuxd_listen_poll();
#endif

	if (sfd < 0) {
		DEBUG(1, "%s: ERROR: usbmuxd was supposed to be running here...\n", __func__);
		return sfd;
	}

	tag = ++use_tag;
	if (send_listen_packet(sfd, tag) <= 0) {
		DEBUG(1, "%s: ERROR: could not send listen packet\n", __func__);
		socket_close(sfd);
		return -1;
	}
	if ((usbmuxd_get_result(sfd, tag, &res, NULL) == 1) && (res != 0)) {
		socket_close(sfd);
		if ((res == RESULT_BADVERSION) && (proto_version == 1)) {
			proto_version = 0;
			goto retry;
		}
		DEBUG(1, "%s: ERROR: did not get OK but %d\n", __func__, res);
		return -1;
	}
	return sfd;
}

/**
 * Waits for an event to occur, i.e. a packet coming from usbmuxd.
 * Calls generate_event to pass the event via callback to the client program.
 */
static int get_next_event(int sfd, usbmuxd_event_cb_t callback, void *user_data)
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
			free(dev);
		} ENDFOREACH
		return -EIO;
	}

	if ((hdr.length > sizeof(hdr)) && !payload) {
		DEBUG(1, "%s: Invalid packet received, payload is missing!\n", __func__);
		return -EBADMSG;
	}

	if ((hdr.message == MESSAGE_DEVICE_ADD) || (hdr.message == MESSAGE_DEVICE_TRUST_PENDING) ||
		(hdr.message == MESSAGE_DEVICE_PASSWORD_PROTECTED) || (hdr.message == MESSAGE_DEVICE_USER_DENIED_PAIRING))
	{
		struct usbmuxd_device_record *dev = payload;
		usbmuxd_device_info_t *devinfo = (usbmuxd_device_info_t*)malloc(sizeof(usbmuxd_device_info_t));
		if (!devinfo) {
			DEBUG(1, "%s: Out of memory!\n", __func__);
			free(payload);
			return -1;
		}

		devinfo->handle = dev->device_id;
		devinfo->product_id = dev->product_id;
		devinfo->location = dev->location;
		memset(devinfo->udid, '\0', sizeof(devinfo->udid));
		memcpy(devinfo->udid, dev->serial_number, sizeof(devinfo->udid));

		if (strcasecmp(devinfo->udid, "ffffffffffffffffffffffffffffffffffffffff") == 0) {
			sprintf(devinfo->udid + 32, "%08x", devinfo->handle);
		}

		/* Add */
		if (hdr.message == MESSAGE_DEVICE_ADD) {
			collection_add(&devices, devinfo);
			generate_event(callback, devinfo, UE_DEVICE_ADD, user_data);
		/* Trust pending */
		} else if (hdr.message == MESSAGE_DEVICE_TRUST_PENDING) {
			generate_event(callback, devinfo, UE_DEVICE_TRUST_PENDING, user_data);
		/* Password protected */
		} else if (hdr.message == MESSAGE_DEVICE_PASSWORD_PROTECTED) {
			generate_event(callback, devinfo, UE_DEVICE_PASSWORD_PROTECTED, user_data);
		/* User denied pairing */
		} else if (hdr.message == MESSAGE_DEVICE_USER_DENIED_PAIRING) {
			generate_event(callback, devinfo, UE_DEVICE_USER_DENIED_PAIRING, user_data);
		}
	} else if (hdr.message == MESSAGE_DEVICE_REMOVE) {
		uint32_t handle;
		usbmuxd_device_info_t *devinfo;

		memcpy(&handle, payload, sizeof(uint32_t));

		devinfo = devices_find(handle);
		if (!devinfo) {
			DEBUG(1, "%s: WARNING: got device remove message for handle %d, but couldn't find the corresponding handle in the device list. This event will be ignored.\n", __func__, handle);
		} else {
			generate_event(callback, devinfo, UE_DEVICE_REMOVE, user_data);
			collection_remove(&devices, devinfo);
			free(devinfo);
		}
	} else if (hdr.length > 0) {
		DEBUG(1, "%s: Unexpected message type %d length %d received!\n", __func__, hdr.message, hdr.length);
	}
	if (payload) {
		free(payload);
	}
	return 0;
}

static void device_monitor_cleanup(void* data)
{
	FOREACH(usbmuxd_device_info_t *dev, &devices) {
		collection_remove(&devices, dev);
		free(dev);
	} ENDFOREACH
	collection_free(&devices);

	socket_close(listenfd);
	listenfd = -1;
}

/**
 * Device Monitor thread function.
 *
 * This function sets up a connection to usbmuxd
 */
static void *device_monitor(void *data)
{
	collection_init(&devices);

#ifndef WIN32
	pthread_cleanup_push(device_monitor_cleanup, NULL);
#endif
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

#ifndef WIN32
	pthread_cleanup_pop(1);
#else
	device_monitor_cleanup(NULL);
#endif
	return NULL;
}

int usbmuxd_subscribe(usbmuxd_event_cb_t callback, void *user_data)
{
	int res;

	if (!callback) {
		return -EINVAL;
	}
	event_cb = callback;

#ifdef WIN32
	res = 0;
	devmon = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)device_monitor, user_data, 0, NULL);
	if (devmon == NULL) {
		res = GetLastError();
	}
#else
	res = pthread_create(&devmon, NULL, device_monitor, user_data);
#endif
	if (res != 0) {
		DEBUG(1, "%s: ERROR: Could not start device watcher thread!\n", __func__);
		return res;
	}
	return 0;
}

int usbmuxd_unsubscribe()
{
	event_cb = NULL;

	socket_shutdown(listenfd, SHUT_RDWR);

#ifdef WIN32
	if (devmon != NULL) {
		WaitForSingleObject(devmon, INFINITE);
	}
#else
	if (pthread_kill(devmon, 0) == 0) {
		pthread_cancel(devmon);
		pthread_join(devmon, NULL);
	}
#endif

	return 0;
}

static usbmuxd_device_info_t *device_info_from_device_record(struct usbmuxd_device_record *dev)
{
	if (!dev) {
		return NULL;
	}
	usbmuxd_device_info_t *devinfo = (usbmuxd_device_info_t*)malloc(sizeof(usbmuxd_device_info_t));
	if (!devinfo) {
		DEBUG(1, "%s: Out of memory!\n", __func__);
		return NULL;
	}

	devinfo->handle = dev->device_id;
	devinfo->product_id = dev->product_id;
	devinfo->location = dev->location;
	memset(devinfo->udid, '\0', sizeof(devinfo->udid));
	memcpy(devinfo->udid, dev->serial_number, sizeof(devinfo->udid));

	if (strcasecmp(devinfo->udid, "ffffffffffffffffffffffffffffffffffffffff") == 0) {
		sprintf(devinfo->udid + 32, "%08x", devinfo->handle);
	}

	return devinfo;
}

int usbmuxd_get_device_list(usbmuxd_device_info_t **device_list)
{
	int sfd;
	int tag;
	int listen_success = 0;
	uint32_t res;
	struct collection tmpdevs;
	usbmuxd_device_info_t *newlist = NULL;
	struct usbmuxd_header hdr;
	struct usbmuxd_device_record *dev;
	int dev_cnt = 0;
	void *payload = NULL;

	*device_list = NULL;

retry:
	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		DEBUG(1, "%s: error opening socket!\n", __func__);
		return sfd;
	}

	tag = ++use_tag;
	if ((proto_version == 1) && (try_list_devices)) {
		if (send_list_devices_packet(sfd, tag) > 0) {
			plist_t list = NULL;
			if ((usbmuxd_get_result(sfd, tag, &res, &list) == 1) && (res == 0)) {
				plist_t devlist = plist_dict_get_item(list, "DeviceList");
				if (devlist && plist_get_node_type(devlist) == PLIST_ARRAY) {
					collection_init(&tmpdevs);
					uint32_t numdevs = plist_array_get_size(devlist);
					uint32_t i;
					for (i = 0; i < numdevs; i++) {
						plist_t pdev = plist_array_get_item(devlist, i);
						plist_t props = plist_dict_get_item(pdev, "Properties");
						dev = device_record_from_plist(props);
						usbmuxd_device_info_t *devinfo = device_info_from_device_record(dev);
						free(dev);
						if (!devinfo) {
							DEBUG(1, "%s: can't create device info object\n", __func__);
							plist_free(list);
							return -1;
						}
						collection_add(&tmpdevs, devinfo);
					}
					plist_free(list);
					goto got_device_list;
				}
			} else {
				if (res == RESULT_BADVERSION) {
					proto_version = 0;
				}
				socket_close(sfd);
				try_list_devices = 0;
				plist_free(list);
				goto retry;
			}
			plist_free(list);
		}
	}

	tag = ++use_tag;
	if (send_listen_packet(sfd, tag) > 0) {
		res = -1;
		// get response
		if ((usbmuxd_get_result(sfd, tag, &res, NULL) == 1) && (res == 0)) {
			listen_success = 1;
		} else {
			socket_close(sfd);
			if ((res == RESULT_BADVERSION) && (proto_version == 1)) {
				proto_version = 0;
				goto retry;
			}
			DEBUG(1, "%s: Did not get response to scan request (with result=0)...\n", __func__);
			return res;
		}
	}

	if (!listen_success) {
		DEBUG(1, "%s: Could not send listen request!\n", __func__);
		return -1;
	}

	collection_init(&tmpdevs);

	// receive device list
	while (1) {
		if (receive_packet(sfd, &hdr, &payload, 100) > 0) {
			if (hdr.message == MESSAGE_DEVICE_ADD) {
				dev = payload;

				usbmuxd_device_info_t *devinfo = device_info_from_device_record(dev);
				if (!devinfo) {
					DEBUG(1, "%s: can't create device info object\n", __func__);
					free(payload);
					return -1;
				}
				collection_add(&tmpdevs, devinfo);

			} else if (hdr.message == MESSAGE_DEVICE_REMOVE) {
				uint32_t handle;
				usbmuxd_device_info_t *devinfo = NULL;

				memcpy(&handle, payload, sizeof(uint32_t));

				FOREACH(usbmuxd_device_info_t *di, &tmpdevs) {
					if (di && di->handle == handle) {
						devinfo = di;
						break;
					}
				} ENDFOREACH
				if (devinfo) {
					collection_remove(&tmpdevs, devinfo);
					free(devinfo);
				}
			} else {
				DEBUG(1, "%s: Unexpected message %d\n", __func__, hdr.message);
			}
			if (payload)
				free(payload);
		} else {
			// we _should_ have all of them now.
			// or perhaps an error occured.
			break;
		}
	}

got_device_list:

	// explicitly close connection
	socket_close(sfd);

	// create copy of device info entries from collection
	newlist = (usbmuxd_device_info_t*)malloc(sizeof(usbmuxd_device_info_t) * (collection_count(&tmpdevs) + 1));
	dev_cnt = 0;
	FOREACH(usbmuxd_device_info_t *di, &tmpdevs) {
		if (di) {
			memcpy(&newlist[dev_cnt], di, sizeof(usbmuxd_device_info_t));
			free(di);
			dev_cnt++;
		}
	} ENDFOREACH
	collection_free(&tmpdevs);

	memset(&newlist[dev_cnt], 0, sizeof(usbmuxd_device_info_t));
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

int usbmuxd_get_device_by_udid(const char *udid, usbmuxd_device_info_t *device)
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
	 	if (!udid) {
			device->handle = dev_list[i].handle;
			device->product_id = dev_list[i].product_id;
			device->location = dev_list[i].location;
			strcpy(device->udid, dev_list[i].udid);
			result = 1;
			break;
		}
		if (!strcmp(udid, dev_list[i].udid)) {
			device->handle = dev_list[i].handle;
			device->product_id = dev_list[i].product_id;
			device->location = dev_list[i].location;
			strcpy(device->udid, dev_list[i].udid);
			result = 1;
			break;
		}
	}

	free(dev_list);

	return result;
}

static send_plist_command(plist_t command)
{
	int ret = 0;

	/* Connect to usbmuxd */
	int sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		DEBUG(1, "%s: Error: Connection to usbmuxd failed: %s\n", __func__, strerror(errno));
		return sfd;
	}

	/* Send the command */
	proto_version = 1;
	int tag = ++use_tag;
	if (send_plist_packet(sfd, tag, command) <= 0) {
		DEBUG(1, "%s: Error sending message!\n", __func__);
	} else {
		uint32_t rc = 0;
		ret = usbmuxd_get_result(sfd, tag, &rc, NULL);
		if ((ret == 1) && (rc == 0)) {
			ret = 0;
		} else if (ret == 1) {
			ret = -(int)rc;
			DEBUG(1, "%s: Error: send device command has failed: %d\n", __func__, ret);
		}
	}

	return ret;
}

int usbmuxd_add_device(uint32_t device_location)
{
	if (!device_location) {
		return -EINVAL;
	}

	/* Create the AddDevice command */
	plist_t command = create_plist_message("AddDevice");
	plist_dict_set_item(command, "DeviceLocation", plist_new_uint(device_location));

	int ret = send_plist_command(command);
	plist_free(command);
	
	return ret;
}

int usbmuxd_remove_device(uint32_t device_location)
{
	if (!device_location) {
		return -EINVAL;
	}

	/* Create the AddDevice command */
	plist_t command = create_plist_message("RemoveDevice");
	plist_dict_set_item(command, "DeviceLocation", plist_new_uint(device_location));

	int ret = send_plist_command(command);
	plist_free(command);
	
	return ret;
}

int usbmuxd_set_device_monitoring(uint32_t device_location, uint8_t auto_monitor)
{
	if (!device_location) {
		return -EINVAL;
	}

	/* Create the AddDevice command */
	plist_t command = create_plist_message("DeviceMonitor");
	plist_dict_set_item(command, "DeviceLocation", plist_new_uint(device_location));
	plist_dict_set_item(command, "AutoMonitor", plist_new_bool(auto_monitor));

	int ret = send_plist_command(command);
	plist_free(command);
	
	return ret;
}

int usbmuxd_connect(const int handle, const unsigned short port)
{
	int sfd;
	int tag;
	int connected = 0;
	uint32_t res = -1;

retry:
	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		DEBUG(1, "%s: Error: Connection to usbmuxd failed: %s\n",
				__func__, strerror(errno));
		return sfd;
	}

	tag = ++use_tag;
	if (send_connect_packet(sfd, tag, (uint32_t)handle, (uint16_t)port) <= 0) {
		DEBUG(1, "%s: Error sending connect message!\n", __func__);
	} else {
		// read ACK
		DEBUG(2, "%s: Reading connect result...\n", __func__);
		if (usbmuxd_get_result(sfd, tag, &res, NULL) == 1) {
			if (res == 0) {
				DEBUG(2, "%s: Connect success!\n", __func__);
				connected = 1;
			} else {
				if ((res == RESULT_BADVERSION) && (proto_version == 1)) {
					proto_version = 0;
					socket_close(sfd);
					goto retry;
				}
				DEBUG(1, "%s: Connect failed, Error code=%d\n", __func__, res);
			}
		}
	}

	if (connected) {
		return sfd;
	}

	socket_close(sfd);

	return -1;
}

int usbmuxd_disconnect(int sfd)
{
	return socket_close(sfd);
}

int usbmuxd_send(int sfd, const char *data, uint32_t len, uint32_t *sent_bytes)
{
	int num_sent;

	if (sfd < 0) {
		return -EINVAL;
	}
	
	num_sent = socket_send_all(sfd, (void*)data, len);
	if (num_sent < 0) {
		*sent_bytes = 0;
		num_sent = errno;
		DEBUG(1, "%s: Error %d when sending: %s\n", __func__, num_sent, strerror(num_sent));
		return -num_sent;
	} else if ((uint32_t)num_sent < len) {
		DEBUG(1, "%s: Warning: Did not send enough (only %d of %d)\n", __func__, num_sent, len);
	}

	*sent_bytes = num_sent;

	return 0;
}

int usbmuxd_recv_timeout(int sfd, char *data, uint32_t len, uint32_t *recv_bytes, unsigned int timeout)
{
	int num_recv = socket_receive_timeout(sfd, (void*)data, len, 0, timeout);
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

int usbmuxd_read_buid(char **buid)
{
	int sfd;
	int tag;
	int ret = 0;

	if (!buid) {
		return -EINVAL;
	}
	*buid = NULL;

	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		DEBUG(1, "%s: Error: Connection to usbmuxd failed: %s\n", __func__, strerror(errno));
		return sfd;
	}

	proto_version = 1;
	tag = ++use_tag;
	if (send_read_buid_packet(sfd, tag) <= 0) {
		DEBUG(1, "%s: Error sending ReadBUID message!\n", __func__);
	} else {
		uint32_t rc = 0;
		plist_t pl = NULL;
		ret = usbmuxd_get_result(sfd, tag, &rc, &pl);
		if ((ret == 1) && (rc == 0)) {
			plist_t node = plist_dict_get_item(pl, "BUID");
			if (node && plist_get_node_type(node) == PLIST_STRING) {
				char * buid_str_val = NULL;
				plist_get_string_val(node, &buid_str_val);
				*buid = strdup(buid_str_val);
				plist_free_memory(buid_str_val);
			}
		} else if (ret == 1) {
			ret = -(int)rc;
		}
		plist_free(pl);
	}

	return ret;
}

int usbmuxd_read_pair_record(const char* record_id, char **record_data, uint32_t *record_size)
{
	int sfd;
	int tag;
	int ret = -1;

	if (!record_id || !record_data || !record_size) {
		return -EINVAL;
	}
	*record_data = NULL;
	*record_size = 0;

	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		DEBUG(1, "%s: Error: Connection to usbmuxd failed: %s\n",
				__func__, strerror(errno));
		return sfd;
	}

	proto_version = 1;
	tag = ++use_tag;

	if (send_pair_record_packet(sfd, tag, "ReadPairRecord", record_id, NULL) <= 0) {
		DEBUG(1, "%s: Error sending ReadPairRecord message!\n", __func__);
	} else {
		uint32_t rc = 0;
		plist_t pl = NULL;
		ret = usbmuxd_get_result(sfd, tag, &rc, &pl);
		if ((ret == 1) && (rc == 0)) {
			plist_t node = plist_dict_get_item(pl, "PairRecordData");
			if (node && plist_get_node_type(node) == PLIST_DATA) {
				uint64_t int64val = 0;
				plist_get_data_val(node, record_data, &int64val);
				if (record_data && int64val > 0) {
					*record_size = (uint32_t)int64val;
					ret = 0;
				}
			}
		} else if (ret == 1) {
			ret = -(int)rc;
		}
		plist_free(pl);
	}

	return ret;
}

int usbmuxd_save_pair_record(const char* record_id, const char *record_data, uint32_t record_size)
{
	int sfd;
	int tag;
	int ret = -1;

	if (!record_id || !record_data || !record_size) {
		return -EINVAL;
	}

	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		DEBUG(1, "%s: Error: Connection to usbmuxd failed: %s\n",
				__func__, strerror(errno));
		return sfd;
	}

	proto_version = 1;
	tag = ++use_tag;

	plist_t data = plist_new_data(record_data, record_size);
	if (send_pair_record_packet(sfd, tag, "SavePairRecord", record_id, data) <= 0) {
		DEBUG(1, "%s: Error sending SavePairRecord message!\n", __func__);
	} else {
		uint32_t rc = 0;
		ret = usbmuxd_get_result(sfd, tag, &rc, NULL);
		if ((ret == 1) && (rc == 0)) {
			ret = 0;
		} else if (ret == 1) {
			ret = -(int)rc;
			DEBUG(1, "%s: Error: saving pair record failed: %d\n", __func__, ret);
		}
	}
	plist_free(data);

	return ret;
}

int usbmuxd_delete_pair_record(const char* record_id)
{
	int sfd;
	int tag;
	int ret = -1;

	if (!record_id) {
		return -EINVAL;
	}

	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		DEBUG(1, "%s: Error: Connection to usbmuxd failed: %s\n",
				__func__, strerror(errno));
		return sfd;
	}

	proto_version = 1;
	tag = ++use_tag;

	if (send_pair_record_packet(sfd, tag, "DeletePairRecord", record_id, NULL) <= 0) {
		DEBUG(1, "%s: Error sending DeletePairRecord message!\n", __func__);
	} else {
		uint32_t rc = 0;
		ret = usbmuxd_get_result(sfd, tag, &rc, NULL);
		if ((ret == 1) && (rc == 0)) {
			ret = 0;
		} else if (ret == 1) {
			ret = -(int)rc;
			DEBUG(1, "%s: Error: deleting pair record failed: %d\n", __func__, ret);
		}
	}

	return ret;
}

void libusbmuxd_set_use_inotify(int set)
{
#ifdef HAVE_INOTIFY
	use_inotify = set;
#endif
	return;
}

void libusbmuxd_set_debug_level(int level)
{
	libusbmuxd_debug = level;
	socket_set_verbose(level);
}

void libusbmuxd_set_socket_port(uint16_t port)
{
	usbmuxd_port = port;
}

uint16_t libusbmuxd_get_socket_port()
{
	return usbmuxd_port;
}

