/*
	usbmuxd - iPhone/iPod Touch USB multiplex server daemon

Copyright (C) 2009	Hector Martin "marcan" <hector@marcansoft.com>
Copyright (C) 2009	Nikias Bassen <nikias@gmx.li>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 or version 3.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

#ifdef HAVE_PLIST
#include <plist/plist.h>
#endif

#include "log.h"
#include "usb.h"
#include "client.h"
#include "device.h"

#ifdef HAVE_PLIST
#define CMD_BUF_SIZE	1024
#else
#define CMD_BUF_SIZE	256
#endif
#define REPLY_BUF_SIZE	1024

enum client_state {
	CLIENT_COMMAND,		// waiting for command
	CLIENT_LISTEN,		// listening for devices
	CLIENT_CONNECTING1,	// issued connection request
	CLIENT_CONNECTING2,	// connection established, but waiting for response message to get sent
	CLIENT_CONNECTED,	// connected
	CLIENT_DEAD
};

struct mux_client {
	int fd;
	unsigned char *ob_buf;
	uint32_t ob_size;
	uint32_t ob_capacity;
	unsigned char *ib_buf;
	uint32_t ib_size;
	uint32_t ib_capacity;
	short events, devents;
	uint32_t connect_tag;
	int connect_device;
	enum client_state state;
	uint32_t proto_version;
};

static struct collection client_list;

int client_read(struct mux_client *client, void *buffer, uint32_t len)
{
	usbmuxd_log(LL_SPEW, "client_read fd %d buf %p len %d", client->fd, buffer, len);
	if(client->state != CLIENT_CONNECTED) {
		usbmuxd_log(LL_ERROR, "Attempted to read from client %d not in CONNECTED state", client->fd);
		return -1;
	}
	return recv(client->fd, buffer, len, 0);
}

int client_write(struct mux_client *client, void *buffer, uint32_t len)
{
	usbmuxd_log(LL_SPEW, "client_write fd %d buf %p len %d", client->fd, buffer, len);
	if(client->state != CLIENT_CONNECTED) {
		usbmuxd_log(LL_ERROR, "Attempted to write to client %d not in CONNECTED state", client->fd);
		return -1;
	}
	return send(client->fd, buffer, len, 0);
}

int client_set_events(struct mux_client *client, short events)
{
	if((client->state != CLIENT_CONNECTED) && (client->state != CLIENT_CONNECTING2)) {
		usbmuxd_log(LL_ERROR, "client_set_events to client %d not in CONNECTED state", client->fd);
		return -1;
	}
	client->devents = events;
	if(client->state == CLIENT_CONNECTED)
		client->events = events;
	return 0;
}

int client_accept(int listenfd)
{
	struct sockaddr_un addr;
	int cfd;
	socklen_t len = sizeof(struct sockaddr_un);
	cfd = accept(listenfd, (struct sockaddr *)&addr, &len);
	if (cfd < 0) {
		usbmuxd_log(LL_ERROR, "accept() failed (%s)", strerror(errno));
		return cfd;
	}

	struct mux_client *client;
	client = malloc(sizeof(struct mux_client));
	memset(client, 0, sizeof(struct mux_client));

	client->fd = cfd;
	client->ob_buf = malloc(REPLY_BUF_SIZE);
	client->ob_size = 0;
	client->ob_capacity = REPLY_BUF_SIZE;
	client->ib_buf = malloc(CMD_BUF_SIZE);
	client->ib_size = 0;
	client->ib_capacity = CMD_BUF_SIZE;
	client->state = CLIENT_COMMAND;
	client->events = POLLIN;

	collection_add(&client_list, client);

	usbmuxd_log(LL_INFO, "New client on fd %d", client->fd);
	return client->fd;
}

void client_close(struct mux_client *client)
{
	usbmuxd_log(LL_INFO, "Disconnecting client fd %d", client->fd);
	if(client->state == CLIENT_CONNECTING1 || client->state == CLIENT_CONNECTING2) {
		usbmuxd_log(LL_INFO, "Client died mid-connect, aborting device %d connection", client->connect_device);
		client->state = CLIENT_DEAD;
		device_abort_connect(client->connect_device, client);
	}
	close(client->fd);
	if(client->ob_buf)
		free(client->ob_buf);
	if(client->ib_buf)
		free(client->ib_buf);
	collection_remove(&client_list, client);
	free(client);
}

void client_get_fds(struct fdlist *list)
{
	FOREACH(struct mux_client *client, &client_list) {
		fdlist_add(list, FD_CLIENT, client->fd, client->events);
	} ENDFOREACH
}

static int send_pkt(struct mux_client *client, uint32_t tag, enum usbmuxd_msgtype msg, void *payload, int payload_length)
{
	struct usbmuxd_header hdr;
	hdr.version = client->proto_version;
	hdr.length = sizeof(hdr) + payload_length;
	hdr.message = msg;
	hdr.tag = tag;
	usbmuxd_log(LL_DEBUG, "send_pkt fd %d tag %d msg %d payload_length %d", client->fd, tag, msg, payload_length);
	if((client->ob_capacity - client->ob_size) < hdr.length) {
		usbmuxd_log(LL_ERROR, "Client %d output buffer full (%d bytes) while sending message %d (%d bytes)", client->fd, client->ob_capacity, hdr.message, hdr.length);
		client_close(client);
		return -1;
	}
	memcpy(client->ob_buf + client->ob_size, &hdr, sizeof(hdr));
	if(payload && payload_length)
		memcpy(client->ob_buf + client->ob_size + sizeof(hdr), payload, payload_length);
	client->ob_size += hdr.length;
	client->events |= POLLOUT;
	return hdr.length;
}

static int send_result(struct mux_client *client, uint32_t tag, uint32_t result)
{
	int res = -1;
#ifdef HAVE_PLIST
	if (client->proto_version == 1) {
		/* XML plist packet */
		char *xml = NULL;
		uint32_t xmlsize = 0;
		plist_t dict = plist_new_dict();
		plist_dict_insert_item(dict, "MessageType", plist_new_string("Result"));
		plist_dict_insert_item(dict, "Number", plist_new_uint(result));
		plist_to_xml(dict, &xml, &xmlsize);
		plist_free(dict);
		if (xml) {
			res = send_pkt(client, tag, MESSAGE_PLIST, xml, xmlsize);
			free(xml);
		} else {
			usbmuxd_log(LL_ERROR, "%s: Could not convert plist to xml", __func__);
		}
	} else
#endif
	{
		/* binary packet */
		res = send_pkt(client, tag, MESSAGE_RESULT, &result, sizeof(uint32_t));
	}
	return res;
}

int client_notify_connect(struct mux_client *client, enum usbmuxd_result result)
{
	usbmuxd_log(LL_SPEW, "client_notify_connect fd %d result %d", client->fd, result);
	if(client->state == CLIENT_DEAD)
		return -1;
	if(client->state != CLIENT_CONNECTING1) {
		usbmuxd_log(LL_ERROR, "client_notify_connect when client %d is not in CONNECTING1 state", client->fd);
		return -1;
	}
	if(send_result(client, client->connect_tag, result) < 0)
		return -1;
	if(result == RESULT_OK) {
		client->state = CLIENT_CONNECTING2;
		client->events = POLLOUT; // wait for the result packet to go through
		// no longer need this
		free(client->ib_buf);
		client->ib_buf = NULL;
	} else {
		client->state = CLIENT_COMMAND;
	}
	return 0;
}

static int notify_device_add(struct mux_client *client, struct device_info *dev)
{
	int res = -1;
#ifdef HAVE_PLIST
	if (client->proto_version == 1) {
		/* XML plist packet */
		char *xml = NULL;
		uint32_t xmlsize = 0;
		plist_t dict = plist_new_dict();
		plist_dict_insert_item(dict, "MessageType", plist_new_string("Attached"));
		plist_t props = plist_new_dict();
		// TODO: get current usb speed
		plist_dict_insert_item(props, "ConnectionSpeed", plist_new_uint(480000000));
		plist_dict_insert_item(props, "ConnectionType", plist_new_string("USB"));
		plist_dict_insert_item(props, "DeviceID", plist_new_uint(dev->id));
		plist_dict_insert_item(props, "LocationID", plist_new_uint(dev->location));
		plist_dict_insert_item(props, "ProductID", plist_new_uint(dev->pid));
		plist_dict_insert_item(props, "SerialNumber", plist_new_string(dev->serial));
		plist_dict_insert_item(dict, "Properties", props);
		plist_to_xml(dict, &xml, &xmlsize);
		plist_free(dict);
		if (xml) {
			res = send_pkt(client, 0, MESSAGE_PLIST, xml, xmlsize);
			free(xml);
		} else {
			usbmuxd_log(LL_ERROR, "%s: Could not convert plist to xml", __func__);
		}
	} else
#endif
	{
		/* binary packet */
		struct usbmuxd_device_record dmsg;
		memset(&dmsg, 0, sizeof(dmsg));
		dmsg.device_id = dev->id;
		strncpy(dmsg.serial_number, dev->serial, 256);
		dmsg.serial_number[255] = 0;
		dmsg.location = dev->location;
		dmsg.product_id = dev->pid;
		res = send_pkt(client, 0, MESSAGE_DEVICE_ADD, &dmsg, sizeof(dmsg));
	}
	return res;
}

static int notify_device_remove(struct mux_client *client, uint32_t device_id)
{
	int res = -1;
#ifdef HAVE_PLIST
	if (client->proto_version == 1) {
		/* XML plist packet */
		char *xml = NULL;
		uint32_t xmlsize = 0;
		plist_t dict = plist_new_dict();
		plist_dict_insert_item(dict, "MessageType", plist_new_string("Detached"));
		plist_dict_insert_item(dict, "DeviceID", plist_new_uint(device_id));
		plist_to_xml(dict, &xml, &xmlsize);
		plist_free(dict);
		if (xml) {
			res = send_pkt(client, 0, MESSAGE_PLIST, xml, xmlsize);
			free(xml);
		} else {
			usbmuxd_log(LL_ERROR, "%s: Could not convert plist to xml", __func__);
		}
	} else
#endif
	{
		/* binary packet */
		res = send_pkt(client, 0, MESSAGE_DEVICE_REMOVE, &device_id, sizeof(uint32_t));
	}
	return res;
}

static int start_listen(struct mux_client *client)
{
	struct device_info *devs;
	struct device_info *dev;
	int count, i;

	client->state = CLIENT_LISTEN;
	count = device_get_count();
	if(!count)
		return 0;
	devs = malloc(sizeof(struct device_info) * count);
	count = device_get_list(devs);

	// going to need a larger buffer for many devices
	int needed_buffer = count * (sizeof(struct usbmuxd_device_record) + sizeof(struct usbmuxd_header)) + REPLY_BUF_SIZE;
	if(client->ob_capacity < needed_buffer) {
		usbmuxd_log(LL_DEBUG, "Enlarging client %d reply buffer %d -> %d to make space for device notifications", client->fd, client->ob_capacity, needed_buffer);
		client->ob_buf = realloc(client->ob_buf, needed_buffer);
		client->ob_capacity = needed_buffer;
	}
	dev = devs;
	for(i=0; i<count; i++) {
		if(notify_device_add(client, dev++) < 0) {
			free(devs);
			return -1;
		}
	}
	free(devs);
	return count;
}

static int client_command(struct mux_client *client, struct usbmuxd_header *hdr)
{
	int res;
	usbmuxd_log(LL_DEBUG, "Client command in fd %d len %d ver %d msg %d tag %d", client->fd, hdr->length, hdr->version, hdr->message, hdr->tag);

	if(client->state != CLIENT_COMMAND) {
		usbmuxd_log(LL_ERROR, "Client %d command received in the wrong state", client->fd);
		if(send_result(client, hdr->tag, RESULT_BADCOMMAND) < 0)
			return -1;
		client_close(client);
		return -1;
	}

	struct usbmuxd_connect_request *ch;
#ifdef HAVE_PLIST
	char *payload;
	uint32_t payload_size;
#endif

	switch(hdr->message) {
#ifdef HAVE_PLIST
		case MESSAGE_PLIST:
			client->proto_version = 1;
			payload = (char*)(hdr) + sizeof(struct usbmuxd_header);
			payload_size = hdr->length - sizeof(struct usbmuxd_header);
			plist_t dict = NULL;
			plist_from_xml(payload, payload_size, &dict);
			if (!dict) {
				usbmuxd_log(LL_ERROR, "Could not parse plist from payload!");
				return -1;
			} else {
				char *message = NULL;
				plist_t node = plist_dict_get_item(dict, "MessageType");
				plist_get_string_val(node, &message);
				if (!message) {
					usbmuxd_log(LL_ERROR, "Could not extract MessageType from plist!");
					plist_free(dict);
					return -1;
				}
				if (!strcmp(message, "Listen")) {
					free(message);
					plist_free(dict);
					if (send_result(client, hdr->tag, 0) < 0)
						return -1;
					usbmuxd_log(LL_DEBUG, "Client %d now LISTENING", client->fd);
					return start_listen(client);
				} else if (!strcmp(message, "Connect")) {
					uint64_t val;
					uint16_t portnum = 0;
					uint32_t device_id = 0;
					free(message);
					// get device id
					node = plist_dict_get_item(dict, "DeviceID");
					if (!node) {
						usbmuxd_log(LL_ERROR, "Received connect request without device_id!");
						plist_free(dict);
						if (send_result(client, hdr->tag, RESULT_BADDEV) < 0)
							return -1;
						return 0;
					}
					val = 0;
					plist_get_uint_val(node, &val);
					device_id = (uint32_t)val;

					// get port number
					node = plist_dict_get_item(dict, "PortNumber");
					if (!node) {
						usbmuxd_log(LL_ERROR, "Received connect request without port number!");
						plist_free(dict);
						if (send_result(client, hdr->tag, RESULT_BADCOMMAND) < 0)
							return -1;
						return 0;
					}
					val = 0;
					plist_get_uint_val(node, &val);
					portnum = (uint16_t)val;

					usbmuxd_log(LL_DEBUG, "Client %d connection request to device %d port %d", client->fd, device_id, ntohs(portnum));
					res = device_start_connect(device_id, ntohs(portnum), client);
					if(res < 0) {
						if (send_result(client, hdr->tag, -res) < 0)
							return -1;
					} else {
						client->connect_tag = hdr->tag;
						client->connect_device = device_id;
						client->state = CLIENT_CONNECTING1;
					}
					return 0;
				} else {
					usbmuxd_log(LL_ERROR, "Unexpected command '%s' received!", message);
					free(message);
					plist_free(dict);
					if (send_result(client, hdr->tag, RESULT_BADCOMMAND) < 0)
						return -1;
					return 0;
				}
			}
			// should not be reached?!
			return -1;
#endif
		case MESSAGE_LISTEN:
			if(send_result(client, hdr->tag, 0) < 0)
				return -1;
			usbmuxd_log(LL_DEBUG, "Client %d now LISTENING", client->fd);
			return start_listen(client);
		case MESSAGE_CONNECT:
			ch = (void*)hdr;
			usbmuxd_log(LL_DEBUG, "Client %d connection request to device %d port %d", client->fd, ch->device_id, ntohs(ch->port));
			res = device_start_connect(ch->device_id, ntohs(ch->port), client);
			if(res < 0) {
				if(send_result(client, hdr->tag, -res) < 0)
					return -1;
			} else {
				client->connect_tag = hdr->tag;
				client->connect_device = ch->device_id;
				client->state = CLIENT_CONNECTING1;
			}
			return 0;
		default:
			usbmuxd_log(LL_ERROR, "Client %d invalid command %d", client->fd, hdr->message);
			if(send_result(client, hdr->tag, RESULT_BADCOMMAND) < 0)
				return -1;
			return 0;
	}
	return -1;
}

static void process_send(struct mux_client *client)
{
	int res;
	if(!client->ob_size) {
		usbmuxd_log(LL_WARNING, "Client %d OUT process but nothing to send?", client->fd);
		client->events &= ~POLLOUT;
		return;
	}
	res = send(client->fd, client->ob_buf, client->ob_size, 0);
	if(res <= 0) {
		usbmuxd_log(LL_ERROR, "Send to client fd %d failed: %d %s", client->fd, res, strerror(errno));
		client_close(client);
		return;
	}
	if(res == client->ob_size) {
		client->ob_size = 0;
		client->events &= ~POLLOUT;
		if(client->state == CLIENT_CONNECTING2) {
			usbmuxd_log(LL_DEBUG, "Client %d switching to CONNECTED state", client->fd);
			client->state = CLIENT_CONNECTED;
			client->events = client->devents;
			// no longer need this
			free(client->ob_buf);
			client->ob_buf = NULL;
		}
	} else {
		client->ob_size -= res;
		memmove(client->ob_buf, client->ob_buf + res, client->ob_size);
	}
}
static void process_recv(struct mux_client *client)
{
	int res;
	int did_read = 0;
	if(client->ib_size < sizeof(struct usbmuxd_header)) {
		res = recv(client->fd, client->ib_buf + client->ib_size, sizeof(struct usbmuxd_header) - client->ib_size, 0);
		if(res <= 0) {
			if(res < 0)
				usbmuxd_log(LL_ERROR, "Receive from client fd %d failed: %s", client->fd, strerror(errno));
			else
				usbmuxd_log(LL_INFO, "Client %d connection closed", client->fd);
			client_close(client);
			return;
		}
		client->ib_size += res;
		if(client->ib_size < sizeof(struct usbmuxd_header))
			return;
		did_read = 1;
	}
	struct usbmuxd_header *hdr = (void*)client->ib_buf;
#ifdef HAVE_PLIST
	if((hdr->version != 0) && (hdr->version != 1)) {
		usbmuxd_log(LL_INFO, "Client %d version mismatch: expected 0 or 1, got %d", client->fd, hdr->version);
#else
	if(hdr->version != USBMUXD_PROTOCOL_VERSION) {
		usbmuxd_log(LL_INFO, "Client %d version mismatch: expected %d, got %d", client->fd, USBMUXD_PROTOCOL_VERSION, hdr->version);
#endif
		client_close(client);
		return;
	}
	if(hdr->length > client->ib_capacity) {
		usbmuxd_log(LL_INFO, "Client %d message is too long (%d bytes)", client->fd, hdr->length);
		client_close(client);
		return;
	}
	if(hdr->length < sizeof(struct usbmuxd_header)) {
		usbmuxd_log(LL_ERROR, "Client %d message is too short (%d bytes)", client->fd, hdr->length);
		client_close(client);
		return;
	}
	if(client->ib_size < hdr->length) {
		if(did_read)
			return; //maybe we would block, so defer to next loop
		res = recv(client->fd, client->ib_buf + client->ib_size, hdr->length - client->ib_size, 0);
		if(res < 0) {
			usbmuxd_log(LL_ERROR, "Receive from client fd %d failed: %s", client->fd, strerror(errno));
			client_close(client);
			return;
		} else if(res == 0) {
			usbmuxd_log(LL_INFO, "Client %d connection closed", client->fd);
			client_close(client);
			return;
		}
		client->ib_size += res;
		if(client->ib_size < hdr->length)
			return;
	}
	client_command(client, hdr);
	client->ib_size = 0;
}

void client_process(int fd, short events)
{
	struct mux_client *client = NULL;
	FOREACH(struct mux_client *lc, &client_list) {
		if(lc->fd == fd) {
			client = lc;
			break;
		}
	} ENDFOREACH

	if(!client) {
		usbmuxd_log(LL_INFO, "client_process: fd %d not found in client list", fd);
		return;
	}

	if(client->state == CLIENT_CONNECTED) {
		usbmuxd_log(LL_SPEW, "client_process in CONNECTED state");
		device_client_process(client->connect_device, client, events);
	} else {
		if(events & POLLIN) {
			process_recv(client);
		} else if(events & POLLOUT) { //not both in case client died as part of process_recv
			process_send(client);
		}
	}

}

void client_device_add(struct device_info *dev)
{
	usbmuxd_log(LL_DEBUG, "client_device_add: id %d, location 0x%x, serial %s", dev->id, dev->location, dev->serial);
	FOREACH(struct mux_client *client, &client_list) {
		if(client->state == CLIENT_LISTEN)
			notify_device_add(client, dev);
	} ENDFOREACH
}
void client_device_remove(int device_id)
{
	uint32_t id = device_id;
	usbmuxd_log(LL_DEBUG, "client_device_remove: id %d", device_id);
	FOREACH(struct mux_client *client, &client_list) {
		if(client->state == CLIENT_LISTEN)
			notify_device_remove(client, id);
	} ENDFOREACH
}


void client_init(void)
{
	usbmuxd_log(LL_DEBUG, "client_init");
	collection_init(&client_list);
}

void client_shutdown(void)
{
	usbmuxd_log(LL_DEBUG, "client_shutdown");
	FOREACH(struct mux_client *client, &client_list) {
		client_close(client);
	} ENDFOREACH
	collection_free(&client_list);
}
