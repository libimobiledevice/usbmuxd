#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

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
#ifdef WINDOWS
	return connect_socket("127.0.0.1", 27015);
#else
	return connect_unix_socket(USBMUXD_SOCKET_FILE);
#endif
}

/**
 * Retrieves the result code to a previously sent request.
 */
static int usbmuxd_get_result(int sfd, uint32_t tag, uint32_t * result)
{
	struct usbmuxd_result_msg res;
	int recv_len;

	if (!result) {
		return -EINVAL;
	}

	if ((recv_len = recv_buf(sfd, &res, sizeof(res))) <= 0) {
		perror("recv");
		return -errno;
	} else {
		if ((recv_len == sizeof(res))
			&& (res.header.length == (uint32_t) recv_len)
			&& (res.header.version == USBMUXD_PROTOCOL_VERSION)
			&& (res.header.message == MESSAGE_RESULT)
			) {
			*result = res.result;
			if (res.header.tag == tag) {
				return 1;
			} else {
				return 0;
			}
		}
	}

	return -1;
}

/**
 * Generates an event, i.e. calls the callback function.
 * A reference to a populated usbmuxd_event_t with information about the event
 * and the corresponding device will be passed to the callback function.
 */
static void generate_event(usbmuxd_event_cb_t callback, const usbmuxd_device_info_t *dev, enum usbmuxd_device_event event)
{
	usbmuxd_event_t ev;

	if (!callback || !dev) {
		return;
	}

	ev.event = event;
	memcpy(&ev.device, dev, sizeof(usbmuxd_device_info_t));

	printf("%s: event=%d, handle=%d\n", __func__, ev.event, ev.device.handle);

	callback(&ev);
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
	struct usbmuxd_listen_request req;
	struct usbmuxd_header hdr;

	req.header.length = sizeof(struct usbmuxd_listen_request);
	req.header.version = USBMUXD_PROTOCOL_VERSION;
	req.header.message = MESSAGE_LISTEN;
	req.header.tag = 2;

	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		fprintf(stderr, "DEBUG: waiting for usbmuxd to come up.\n");

		while (event_cb) {
			if ((sfd = connect_usbmuxd_socket()) > 0) {
				fprintf(stderr, "DEBUG: usbmuxd started\n");
				break;
			}
			sleep(1);
		}
	}

	if (sfd < 0) {
		fprintf(stderr, "ERROR: usbmuxd was supposed to be running here...\n");
		return sfd;
	}

	if (send_buf(sfd, &req, req.header.length) != (int)req.header.length) {
		fprintf(stderr, "ERROR: could not send listen packet\n");
		close(sfd);
		return -1;
	}
	if (usbmuxd_get_result(sfd, req.header.tag, &res) && (res != 0)) {
		fprintf(stderr, "ERROR: did not get OK\n");
		close(sfd);
		return -1;
	}

	return sfd;
}

/**
 * Waits for an event to occur, i.e. a packet coming from usbmuxd.
 * Calls generate_event to pass the event via callback to the client program.
 */
int get_next_event(int sfd, usbmuxd_event_cb_t callback)
{
	int recv_len;
	struct usbmuxd_listen_request req;
	struct usbmuxd_header hdr;

	/* block until we receive something */
	recv_len = recv_buf_timeout(sfd, &hdr, sizeof(hdr), 0, 0);
	if (recv_len < 0) {
		int i;
		fprintf(stderr, "DEBUG: connection closed.\n");
		// when then usbmuxd connection fails,
		// generate remove events for every device that
		// is still present so applications know about it
		// TODO: is this behaviour correct?
		FOREACH(usbmuxd_device_info_t *dev, &devices) {
			generate_event(callback, dev, UE_DEVICE_REMOVE);
		} ENDFOREACH
		collection_free(&devices);
		return recv_len;
	} else if (recv_len == sizeof(hdr)) {
		if (hdr.message == MESSAGE_DEVICE_ADD) {
			struct usbmuxd_device_record dev;
			usbmuxd_device_info_t *devinfo = (usbmuxd_device_info_t*)malloc(sizeof(usbmuxd_device_info_t));
			if (!devinfo) {
				fprintf(stderr, "Out of memory!\n");
				return -1;
			}

			if (hdr.length != sizeof(struct usbmuxd_header)+sizeof(struct usbmuxd_device_record)) {
				fprintf(stderr, "WARNING: unexpected packet size%d for MESSAGE_DEVICE_ADD (expected %d)!\n", hdr.length, sizeof(struct usbmuxd_header)+sizeof(struct usbmuxd_device_record));
			}
			recv_len =  recv_buf_timeout(sfd, &dev, hdr.length - sizeof(struct usbmuxd_header), 0, 5000);
			if (recv_len != (hdr.length - sizeof(struct usbmuxd_header))) {
				fprintf(stderr, "Could not receive packet\n");
				return recv_len;
			}

			devinfo->handle = dev.device_id;
			devinfo->product_id = dev.product_id;
			memset(devinfo->uuid, '\0', sizeof(devinfo->uuid));
			memcpy(devinfo->uuid, dev.serial_number, sizeof(devinfo->uuid));

			collection_add(&devices, devinfo);
			generate_event(callback, devinfo, UE_DEVICE_ADD);
		} else if (hdr.message == MESSAGE_DEVICE_REMOVE) {
			uint32_t handle;
			usbmuxd_device_info_t *dev;

			if (hdr.length != sizeof(struct usbmuxd_header)+sizeof(uint32_t)) {
				fprintf(stderr, "WARNING: unexpected packet size%d for MESSAGE_DEVICE_REMOVE (expected %d)!\n", hdr.length, sizeof(struct usbmuxd_header)+sizeof(uint32_t));
			}
			recv_len = recv_buf_timeout(sfd, &handle, sizeof(uint32_t), 0, 5000);
			if (recv_len != sizeof(uint32_t)) {
				fprintf(stderr, "Could not receive packet\n");
				return recv_len;
			}

			dev = devices_find(handle);
			if (!dev) {
				fprintf(stderr, "WARNING: got device remove message for handle %d, but couldn't find the corresponding handle in the device list. This event will be ignored.\n", handle);
			} else {
				generate_event(callback, dev, UE_DEVICE_REMOVE);
				collection_remove(&devices, dev);
			}
		} else {
			fprintf(stderr, "%s: Unknown message type %d length %d\n", __func__, hdr.message, hdr.length);
		}
	} else {
		fprintf(stderr, "%s: ERROR: incomplete packet received!\n", __func__);
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
			fprintf(stderr, "DEBUG: listenfd=%d\n", listenfd);
			continue;
		}

		while (event_cb) {
			printf("waiting for events\n");
			int res = get_next_event(listenfd, event_cb);
			if (res < 0) {
			    fprintf(stderr, "%s: closing connection (code %d)\n", __func__, res);
			    break;
			}
		}
	}

	collection_free(&devices);
	printf("%s: terminated\n", __func__);

	return NULL;
}

int usbmuxd_subscribe(usbmuxd_event_cb_t callback)
{
	int res;

	if (!callback) {
		return -EINVAL;
	}
	event_cb = callback;

	res = pthread_create(&devmon, NULL, device_monitor, NULL);
	if (res != 0) {
		fprintf(stderr, "ERROR: Could not start device watcher thread!\n");
		return res;
	}
	return 0;
}

int usbmuxd_unsubscribe()
{
	event_cb = NULL;

	if (pthread_kill(devmon, 0) == 0) {
		printf("%s: unsubscribing callback\n", __func__);
		close(listenfd);
		listenfd = -1;
		pthread_kill(devmon, SIGINT);
		pthread_join(devmon, NULL);
	}

	return 0;
}

int usbmuxd_scan(usbmuxd_device_info_t ** available_devices)
{
	struct usbmuxd_listen_request s_req;
	int sfd;
	int listen_success = 0;
	uint32_t res;
	int recv_len;
	usbmuxd_device_info_t *newlist = NULL;
	struct usbmuxd_header hdr;
	struct usbmuxd_device_record dev_info;
	int dev_cnt = 0;

	sfd = connect_unix_socket(USBMUXD_SOCKET_FILE);
	if (sfd < 0) {
		fprintf(stderr, "%s: error opening socket!\n", __func__);
		return sfd;
	}

	s_req.header.length = sizeof(struct usbmuxd_listen_request);
	s_req.header.version = USBMUXD_PROTOCOL_VERSION;
	s_req.header.message = MESSAGE_LISTEN;
	s_req.header.tag = 2;

	// send scan request packet
	if (send_buf(sfd, &s_req, s_req.header.length) ==
		(int) s_req.header.length) {
		res = -1;
		// get response
		if (usbmuxd_get_result(sfd, s_req.header.tag, &res) && (res == 0)) {
			listen_success = 1;
		} else {
			fprintf(stderr,
					"%s: Did not get response to scan request (with result=0)...\n",
					__func__);
			close(sfd);
			return res;
		}
	}

	if (!listen_success) {
		fprintf(stderr, "%s: Could not send listen request!\n", __func__);
		return -1;
	}

	*available_devices = NULL;
	// receive device list
	while (1) {
		if (recv_buf_timeout(sfd, &hdr, sizeof(hdr), 0, 1000) == sizeof(hdr)) {
			if (hdr.length != sizeof(hdr)+sizeof(dev_info)) {
				// invalid packet size received!
				fprintf(stderr,
						"%s: Invalid packet size (%d) received when expecting a device info record.\n",
						__func__, hdr.length);
				break;
			}

			recv_len = recv_buf(sfd, &dev_info, hdr.length - sizeof(hdr));
			if (recv_len <= 0) {
				fprintf(stderr,
						"%s: Error when receiving device info record\n",
						__func__);
				break;
			} else if ((uint32_t) recv_len < hdr.length - sizeof(hdr)) {
				fprintf(stderr,
						"%s: received less data than specified in header!\n", __func__);
			} else {
				newlist = (usbmuxd_device_info_t *) realloc(*available_devices, sizeof(usbmuxd_device_info_t) * (dev_cnt + 1));
				if (newlist) {
					newlist[dev_cnt].handle =
						(int) dev_info.device_id;
					newlist[dev_cnt].product_id =
						dev_info.product_id;
					memset(newlist[dev_cnt].uuid, '\0',
						   sizeof(newlist[dev_cnt].uuid));
					memcpy(newlist[dev_cnt].uuid,
						   dev_info.serial_number,
						   sizeof(newlist[dev_cnt].uuid));
					*available_devices = newlist;
					dev_cnt++;
				} else {
					fprintf(stderr,
							"%s: ERROR: out of memory when trying to realloc!\n",
							__func__);
					break;
				}
			}
		} else {
			// we _should_ have all of them now.
			// or perhaps an error occured.
			break;
		}
	}

	// terminating zero record
	newlist = (usbmuxd_device_info_t*) realloc(*available_devices, sizeof(usbmuxd_device_info_t) * (dev_cnt + 1));
	memset(newlist + dev_cnt, 0, sizeof(usbmuxd_device_info_t));
	*available_devices = newlist;

	return dev_cnt;
}

int usbmuxd_connect(const int handle, const unsigned short port)
{
	int sfd;
	struct usbmuxd_connect_request c_req;
	int connected = 0;
	uint32_t res = -1;

	sfd = connect_usbmuxd_socket();
	if (sfd < 0) {
		fprintf(stderr, "%s: Error: Connection to usbmuxd failed: %s\n",
				__func__, strerror(errno));
		return sfd;
	}

	c_req.header.length = sizeof(c_req);
	c_req.header.version = USBMUXD_PROTOCOL_VERSION;
	c_req.header.message = MESSAGE_CONNECT;
	c_req.header.tag = 3;
	c_req.device_id = (uint32_t) handle;
	c_req.port = htons(port);
	c_req.reserved = 0;

	if (send_buf(sfd, &c_req, sizeof(c_req)) < 0) {
		perror("send");
	} else {
		// read ACK
		//fprintf(stderr, "%s: Reading connect result...\n", __func__);
		if (usbmuxd_get_result(sfd, c_req.header.tag, &res)) {
			if (res == 0) {
				//fprintf(stderr, "%s: Connect success!\n", __func__);
				connected = 1;
			} else {
				fprintf(stderr, "%s: Connect failed, Error code=%d\n",
						__func__, res);
			}
		}
	}

	if (connected) {
		return sfd;
	}

	close(sfd);

	return -1;
}

int usbmuxd_disconnect(int sfd)
{
	return close(sfd);
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

