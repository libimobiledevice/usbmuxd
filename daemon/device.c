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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#define _BSD_SOURCE

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "device.h"
#include "client.h"
#include "usb.h"
#include "log.h"

int next_device_id;

#define DEV_MRU 65536

#define CONN_INBUF_SIZE		262144
#define CONN_OUTBUF_SIZE	65536

#define ACK_TIMEOUT 30

enum mux_protocol {
	MUX_PROTO_VERSION = 0,
	MUX_PROTO_TCP = IPPROTO_TCP,
};

enum mux_dev_state {
	MUXDEV_INIT,	// sent version packet
	MUXDEV_ACTIVE,	// received version packet, active
	MUXDEV_DEAD		// dead
};

enum mux_conn_state {
	CONN_CONNECTING,	// SYN
	CONN_CONNECTED,		// SYN/SYNACK/ACK -> active
	CONN_REFUSED,		// RST received during SYN
	CONN_DYING,			// RST received
	CONN_DEAD			// being freed; used to prevent infinite recursion between client<->device freeing
};

struct mux_header
{
	uint32_t protocol;
	uint32_t length;
};

struct version_header
{
	uint32_t major;
	uint32_t minor;
	uint32_t padding;
};

struct mux_device;

#define CONN_ACK_PENDING 1

struct mux_connection
{
	struct mux_device *dev;
	struct mux_client *client;
	enum mux_conn_state state;
	uint16_t sport, dport;
	uint32_t tx_seq, tx_ack, tx_acked, tx_win;
	uint32_t rx_seq, rx_recvd, rx_ack, rx_win;
	uint32_t max_payload;
	uint32_t sendable;
	int flags;
	unsigned char *ib_buf;
	uint32_t ib_size;
	uint32_t ib_capacity;
	unsigned char *ob_buf;
	uint32_t ob_capacity;
	short events;
	uint64_t last_ack_time;
};

struct mux_device
{
	struct usb_device *usbdev;
	int id;
	enum mux_dev_state state;
	struct collection connections;
	uint16_t next_sport;
	unsigned char *pktbuf;
	uint32_t pktlen;
};

static struct collection device_list;

uint64_t mstime64(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static int get_next_device_id(void)
{
	while(1) {
		int ok = 1;
		FOREACH(struct mux_device *dev, &device_list) {
			if(dev->id == next_device_id) {
				next_device_id++;
				ok = 0;
				break;
			}
		} ENDFOREACH
		if(ok)
			return next_device_id++;
	}
}

static int send_packet(struct mux_device *dev, enum mux_protocol proto, void *header, const void *data, int length)
{
	unsigned char *buffer;
	int hdrlen;
	int res;

	switch(proto) {
		case MUX_PROTO_VERSION:
			hdrlen = sizeof(struct version_header);
			break;
		case MUX_PROTO_TCP:
			hdrlen = sizeof(struct tcphdr);
			break;
		default:
			usbmuxd_log(LL_ERROR, "Invalid protocol %d for outgoing packet (dev %d hdr %p data %p len %d)", proto, dev->id, header, data, length);
			return -1;
	}
	usbmuxd_log(LL_SPEW, "send_packet(%d, 0x%x, %p, %p, %d)", dev->id, proto, header, data, length);

	int total = sizeof(struct mux_header) + hdrlen + length;

	if(total > USB_MTU) {
		usbmuxd_log(LL_ERROR, "Tried to send packet larger than USB MTU (hdr %d data %d total %d) to device %d", hdrlen, length, total, dev->id);
		return -1;
	}

	buffer = malloc(total);
	struct mux_header *mhdr = (struct mux_header *)buffer;
	mhdr->protocol = htonl(proto);
	mhdr->length = htonl(total);
	memcpy(buffer + sizeof(struct mux_header), header, hdrlen);
	if(data && length)
		memcpy(buffer + sizeof(struct mux_header) + hdrlen, data, length);

	if((res = usb_send(dev->usbdev, buffer, total)) < 0) {
		usbmuxd_log(LL_ERROR, "usb_send failed while sending packet (len %d) to device %d: %d", total, dev->id, res);
		free(buffer);
		return res;
	}
	return total;
}

static uint16_t find_sport(struct mux_device *dev)
{
	if(collection_count(&dev->connections) >= 65535)
		return 0; //insanity

	while(1) {
		int ok = 1;
		FOREACH(struct mux_connection *conn, &dev->connections) {
			if(dev->next_sport == conn->sport) {
				dev->next_sport++;
				ok = 0;
				break;
			}
		} ENDFOREACH
		if(ok)
			return dev->next_sport++;
	}
}

static int send_anon_rst(struct mux_device *dev, uint16_t sport, uint16_t dport, uint32_t ack)
{
	struct tcphdr th;
	memset(&th, 0, sizeof(th));
	th.th_sport = htons(sport);
	th.th_dport = htons(dport);
	th.th_ack = htonl(ack);
	th.th_flags = TH_RST;
	th.th_off = sizeof(th) / 4;

	usbmuxd_log(LL_DEBUG, "[OUT] dev=%d sport=%d dport=%d flags=0x%x", dev->id, sport, dport, th.th_flags);

	int res = send_packet(dev, MUX_PROTO_TCP, &th, NULL, 0);
	return res;
}

static int send_tcp(struct mux_connection *conn, uint8_t flags, const unsigned char *data, int length)
{
	struct tcphdr th;
	memset(&th, 0, sizeof(th));
	th.th_sport = htons(conn->sport);
	th.th_dport = htons(conn->dport);
	th.th_seq = htonl(conn->tx_seq);
	th.th_ack = htonl(conn->tx_ack);
	th.th_flags = flags;
	th.th_off = sizeof(th) / 4;
	th.th_win = htons(conn->tx_win >> 8);

	usbmuxd_log(LL_DEBUG, "[OUT] dev=%d sport=%d dport=%d seq=%d ack=%d flags=0x%x window=%d[%d] len=%d",
		conn->dev->id, conn->sport, conn->dport, conn->tx_seq, conn->tx_ack, flags, conn->tx_win, conn->tx_win >> 8, length);

	int res = send_packet(conn->dev, MUX_PROTO_TCP, &th, data, length);
	if(res >= 0) {
		conn->tx_acked = conn->tx_ack;
		conn->last_ack_time = mstime64();
		conn->flags &= ~CONN_ACK_PENDING;
	}
	return res;
}

static void connection_teardown(struct mux_connection *conn)
{
	int res;
	if(conn->state == CONN_DEAD)
		return;
	usbmuxd_log(LL_DEBUG, "connection_teardown dev %d sport %d dport %d", conn->dev->id, conn->sport, conn->dport);
	if(conn->dev->state != MUXDEV_DEAD && conn->state != CONN_DYING && conn->state != CONN_REFUSED) {
		res = send_tcp(conn, TH_RST, NULL, 0);
		if(res < 0)
			usbmuxd_log(LL_ERROR, "Error sending TCP RST to device %d (%d->%d)", conn->dev->id, conn->sport, conn->dport);
	}
	if(conn->client) {
		if(conn->state == CONN_REFUSED || conn->state == CONN_CONNECTING) {
			client_notify_connect(conn->client, RESULT_CONNREFUSED);
		} else {
			conn->state = CONN_DEAD;
			client_close(conn->client);
		}
	}
	if(conn->ib_buf)
		free(conn->ib_buf);
	if(conn->ob_buf)
		free(conn->ob_buf);
	collection_remove(&conn->dev->connections, conn);
	free(conn);
}

int device_start_connect(int device_id, uint16_t dport, struct mux_client *client)
{
	struct mux_device *dev = NULL;
	FOREACH(struct mux_device *cdev, &device_list) {
		if(cdev->id == device_id) {
			dev = cdev;
			break;
		}
	} ENDFOREACH
	if(!dev) {
		usbmuxd_log(LL_WARNING, "Attempted to connect to nonexistent device %d", device_id);
		return -RESULT_BADDEV;
	}

	uint16_t sport = find_sport(dev);
	if(!sport) {
		usbmuxd_log(LL_WARNING, "Unable to allocate port for device %d", device_id);
		return -RESULT_BADDEV;
	}

	struct mux_connection *conn;
	conn = malloc(sizeof(struct mux_connection));
	memset(conn, 0, sizeof(struct mux_connection));

	conn->dev = dev;
	conn->client = client;
	conn->state = CONN_CONNECTING;
	conn->sport = sport;
	conn->dport = dport;
	conn->tx_seq = 0;
	conn->tx_ack = 0;
	conn->tx_acked = 0;
	conn->tx_win = 131072;
	conn->rx_recvd = 0;
	conn->flags = 0;
	conn->max_payload = USB_MTU - sizeof(struct mux_header) - sizeof(struct tcphdr);

	conn->ob_buf = malloc(CONN_OUTBUF_SIZE);
	conn->ob_capacity = CONN_OUTBUF_SIZE;
	conn->ib_buf = malloc(CONN_INBUF_SIZE);
	conn->ib_capacity = CONN_INBUF_SIZE;
	conn->ib_size = 0;

	int res;

	res = send_tcp(conn, TH_SYN, NULL, 0);
	if(res < 0) {
		usbmuxd_log(LL_ERROR, "Error sending TCP SYN to device %d (%d->%d)", dev->id, sport, dport);
		free(conn);
		return -RESULT_CONNREFUSED; //bleh
	}
	collection_add(&dev->connections, conn);
	return 0;
}

static void update_connection(struct mux_connection *conn)
{
	uint32_t sent = conn->tx_seq - conn->rx_ack;

	if(conn->rx_win > sent)
		conn->sendable = conn->rx_win - sent;
	else
		conn->sendable = 0;

	if(conn->sendable > conn->ob_capacity)
		conn->sendable = conn->ob_capacity;
	if(conn->sendable > conn->max_payload)
		conn->sendable = conn->max_payload;

	if(conn->sendable > 0)
		conn->events |= POLLIN;
	else
		conn->events &= ~POLLIN;

	if(conn->ib_size)
		conn->events |= POLLOUT;
	else
		conn->events &= ~POLLOUT;

	if(conn->tx_acked != conn->tx_ack)
		conn->flags |= CONN_ACK_PENDING;
	else
		conn->flags &= ~CONN_ACK_PENDING;

	usbmuxd_log(LL_SPEW, "update_connection: sendable %d, events %d, flags %d", conn->sendable, conn->events, conn->flags);
	client_set_events(conn->client, conn->events);
}

void device_client_process(int device_id, struct mux_client *client, short events)
{
	struct mux_connection *conn = NULL;
	FOREACH(struct mux_device *dev, &device_list) {
		if(dev->id == device_id) {
			FOREACH(struct mux_connection *lconn, &dev->connections) {
				if(lconn->client == client) {
					conn = lconn;
					break;
				}
			} ENDFOREACH
			break;
		}
	} ENDFOREACH

	if(!conn) {
		usbmuxd_log(LL_WARNING, "Could not find connection for device %d client %p", device_id, client);
		return;
	}
	usbmuxd_log(LL_SPEW, "device_client_process (%d)", events);

	int res;
	int size;
	if(events & POLLOUT) {
		size = client_write(conn->client, conn->ib_buf, conn->ib_size);
		if(size <= 0) {
			usbmuxd_log(LL_DEBUG, "error writing to client (%d)", size);
			connection_teardown(conn);
			return;
		}
		conn->tx_ack += size;
		if(size == conn->ib_size) {
			conn->ib_size = 0;
		} else {
			conn->ib_size -= size;
			memmove(conn->ib_buf, conn->ib_buf + size, conn->ib_size);
		}
	}
	if(events & POLLIN) {
		size = client_read(conn->client, conn->ob_buf, conn->sendable);
		if(size <= 0) {
			usbmuxd_log(LL_DEBUG, "error reading from client (%d)", size);
			connection_teardown(conn);
			return;
		}
		res = send_tcp(conn, TH_ACK, conn->ob_buf, size);
		if(res < 0) {
			connection_teardown(conn);
			return;
		}
		conn->tx_seq += size;
	}

	update_connection(conn);
}

static void connection_device_input(struct mux_connection *conn, unsigned char *payload, uint32_t payload_length)
{
	if((conn->ib_size + payload_length) > conn->ib_capacity) {
		usbmuxd_log(LL_ERROR, "Input buffer overflow on device %d connection %d->%d (space=%d, payload=%d)", conn->dev->id, conn->sport, conn->dport, conn->ib_capacity-conn->ib_size, payload_length);
		connection_teardown(conn);
		return;
	}
	memcpy(conn->ib_buf + conn->ib_size, payload, payload_length);
	conn->ib_size += payload_length;
	conn->rx_recvd += payload_length;
	update_connection(conn);
}

void device_abort_connect(int device_id, struct mux_client *client)
{
	FOREACH(struct mux_device *dev, &device_list) {
		if(dev->id == device_id) {
			FOREACH(struct mux_connection *conn, &dev->connections) {
				if(conn->client == client) {
					connection_teardown(conn);
					return;
				}
			} ENDFOREACH
			usbmuxd_log(LL_WARNING, "Attempted to abort for nonexistent connection for device %d", device_id);
			return;
		}
	} ENDFOREACH
	usbmuxd_log(LL_WARNING, "Attempted to abort connection for nonexistent device %d", device_id);
}

static void device_version_input(struct mux_device *dev, struct version_header *vh)
{
	if(dev->state != MUXDEV_INIT) {
		usbmuxd_log(LL_WARNING, "Version packet from already initialized device %d", dev->id);
		return;
	}
	vh->major = ntohl(vh->major);
	vh->minor = ntohl(vh->minor);
	if(vh->major != 1 || vh->minor != 0) {
		usbmuxd_log(LL_ERROR, "Device %d has unknown version %d.%d\n", dev->id, vh->major, vh->minor);
		collection_remove(&device_list, dev);
		free(dev);
		return;
	}
	usbmuxd_log(LL_NOTICE, "Connected to v%d.%d device %d on location 0x%x with serial number %s", vh->major, vh->minor, dev->id, usb_get_location(dev->usbdev), usb_get_serial(dev->usbdev));
	dev->state = MUXDEV_ACTIVE;
	collection_init(&dev->connections);
	struct device_info info;
	info.id = dev->id;
	info.location = usb_get_location(dev->usbdev);
	info.serial = usb_get_serial(dev->usbdev);
	info.pid = usb_get_pid(dev->usbdev);
	client_device_add(&info);
}

static void device_tcp_input(struct mux_device *dev, struct tcphdr *th, unsigned char *payload, uint32_t payload_length)
{
	uint16_t sport = ntohs(th->th_dport);
	uint16_t dport = ntohs(th->th_sport);
	struct mux_connection *conn = NULL;

	usbmuxd_log(LL_DEBUG, "[IN] dev=%d sport=%d dport=%d seq=%d ack=%d flags=0x%x window=%d[%d] len=%d",
		dev->id, dport, sport, ntohl(th->th_seq), ntohl(th->th_ack), th->th_flags, ntohs(th->th_win) << 8, ntohs(th->th_win), payload_length);

	if(dev->state != MUXDEV_ACTIVE) {
		usbmuxd_log(LL_ERROR, "Received TCP packet from device %d but the device isn't active yet, discarding\n", dev->id);
		return;
	}

	FOREACH(struct mux_connection *lconn, &dev->connections) {
		if(lconn->sport == sport && lconn->dport == dport) {
			conn = lconn;
			break;
		}
	} ENDFOREACH

	if(!conn) {
		usbmuxd_log(LL_INFO, "No connection for device %d incoming packet %d->%d", dev->id, dport, sport);
		if(!(th->th_flags & TH_RST)) {
			if(send_anon_rst(dev, sport, dport, ntohl(th->th_seq)) < 0)
				usbmuxd_log(LL_ERROR, "Error sending TCP RST to device %d (%d->%d)", conn->dev->id, sport, dport);
		}
		return;
	}

	conn->rx_seq = ntohl(th->th_seq);
	conn->rx_ack = ntohl(th->th_ack);
	conn->rx_win = ntohs(th->th_win) << 8;

	if(th->th_flags & TH_RST) {
		char *buf = malloc(payload_length+1);
		memcpy(buf, payload, payload_length);
		if(payload_length && (buf[payload_length-1] == '\n'))
			buf[payload_length-1] = 0;
		buf[payload_length] = 0;
		usbmuxd_log(LL_DEBUG, "RST reason: %s", buf);
		free(buf);
	}

	if(conn->state == CONN_CONNECTING) {
		if(th->th_flags != (TH_SYN|TH_ACK)) {
			if(th->th_flags & TH_RST)
				conn->state = CONN_REFUSED;
			usbmuxd_log(LL_INFO, "Connection refused by device %d (%d->%d)", dev->id, sport, dport);
			connection_teardown(conn); //this also sends the notification to the client
		} else {
			conn->tx_seq++;
			conn->tx_ack++;
			conn->rx_recvd = conn->rx_seq;
			if(send_tcp(conn, TH_ACK, NULL, 0) < 0) {
				usbmuxd_log(LL_ERROR, "Error sending TCP ACK to device %d (%d->%d)", dev->id, sport, dport);
				connection_teardown(conn);
				return;
			}
			conn->state = CONN_CONNECTED;
			if(client_notify_connect(conn->client, RESULT_OK) < 0) {
				conn->client = NULL;
				connection_teardown(conn);
			}
			update_connection(conn);
		}
	} else if(conn->state == CONN_CONNECTED) {
		if(th->th_flags != TH_ACK) {
			usbmuxd_log(LL_INFO, "Connection reset by device %d (%d->%d)", dev->id, sport, dport);
			if(th->th_flags & TH_RST)
				conn->state = CONN_DYING;
			connection_teardown(conn);
		} else {
			connection_device_input(conn, payload, payload_length);
		}
	}
}

void device_data_input(struct usb_device *usbdev, unsigned char *buffer, uint32_t length)
{
	struct mux_device *dev = NULL;
	FOREACH(struct mux_device *tdev, &device_list) {
		if(tdev->usbdev == usbdev) {
			dev = tdev;
			break;
		}
	} ENDFOREACH
	if(!dev) {
		usbmuxd_log(LL_WARNING, "Cannot find device entry for RX input from USB device %p on location 0x%x", usbdev, usb_get_location(usbdev));
		return;
	}

	if(!length)
		return;

	// sanity check (should never happen with current USB implementation)
	if((length > USB_MRU) || (length > DEV_MRU)) {
		usbmuxd_log(LL_ERROR, "Too much data received from USB (%d), file a bug", length);
		return;
	}

	usbmuxd_log(LL_SPEW, "Mux data input for device %p: %p len %d", dev, buffer, length);

	// handle broken up transfers
	if(dev->pktlen) {
		if((length + dev->pktlen) > DEV_MRU) {
			usbmuxd_log(LL_ERROR, "Incoming split packet is too large (%d so far), dropping!", length + dev->pktlen);
			dev->pktlen = 0;
			return;
		}
		memcpy(dev->pktbuf + dev->pktlen, buffer, length);
		struct mux_header *mhdr = (struct mux_header *)dev->pktbuf;
		if((length < USB_MRU) || (ntohl(mhdr->length) == (length + dev->pktlen))) {
			buffer = dev->pktbuf;
			length += dev->pktlen;
			dev->pktlen = 0;
			usbmuxd_log(LL_SPEW, "Gathered mux data from buffer (total size: %d)", length);
		} else {
			dev->pktlen += length;
			usbmuxd_log(LL_SPEW, "Appended mux data to buffer (total size: %d)", dev->pktlen);
			return;
		}
	} else {
		struct mux_header *mhdr = (struct mux_header *)buffer;
		if((length == USB_MRU) && (length < ntohl(mhdr->length))) {
			memcpy(dev->pktbuf, buffer, length);
			dev->pktlen = length;
			usbmuxd_log(LL_SPEW, "Copied mux data to buffer (size: %d)", dev->pktlen);
			return;
		}
	}

	struct mux_header *mhdr = (struct mux_header *)buffer;

	if(ntohl(mhdr->length) != length) {
		usbmuxd_log(LL_ERROR, "Incoming packet size mismatch (dev %d, expected %d, got %d)", dev->id, ntohl(mhdr->length), length);
		return;
	}

	struct tcphdr *th;
	unsigned char *payload;
	uint32_t payload_length;

	switch(ntohl(mhdr->protocol)) {
		case MUX_PROTO_VERSION:
			if(length < (sizeof(struct mux_header) + sizeof(struct version_header))) {
				usbmuxd_log(LL_ERROR, "Incoming version packet is too small (%d)", length);
				return;
			}
			device_version_input(dev, (struct version_header *)(mhdr+1));
			break;
		case MUX_PROTO_TCP:
			if(length < (sizeof(struct mux_header) + sizeof(struct tcphdr))) {
				usbmuxd_log(LL_ERROR, "Incoming TCP packet is too small (%d)", length);
				return;
			}
			th = (struct tcphdr *)(mhdr+1);
			payload = (unsigned char *)(th+1);
			payload_length = length - sizeof(struct tcphdr) - sizeof(struct mux_header);
			device_tcp_input(dev, (struct tcphdr *)(mhdr+1), payload, payload_length);
			break;
		default:
			usbmuxd_log(LL_ERROR, "Incoming packet for device %d has unknown protocol 0x%x)", dev->id, ntohl(mhdr->protocol));
			break;
	}

}

int device_add(struct usb_device *usbdev)
{
	int res;
	int id = get_next_device_id();
	struct mux_device *dev;
	usbmuxd_log(LL_NOTICE, "Connecting to new device on location 0x%x as ID %d", usb_get_location(usbdev), id);
	dev = malloc(sizeof(struct mux_device));
	dev->id = id;
	dev->usbdev = usbdev;
	dev->state = MUXDEV_INIT;
	dev->next_sport = 1;
	dev->pktbuf = malloc(DEV_MRU);
	dev->pktlen = 0;
	struct version_header vh;
	vh.major = htonl(1);
	vh.minor = htonl(0);
	vh.padding = 0;
	if((res = send_packet(dev, MUX_PROTO_VERSION, &vh, NULL, 0)) < 0) {
		usbmuxd_log(LL_ERROR, "Error sending version request packet to device %d", id);
		free(dev);
		return res;
	}
	collection_add(&device_list, dev);
	return 0;
}

void device_remove(struct usb_device *usbdev)
{
	FOREACH(struct mux_device *dev, &device_list) {
		if(dev->usbdev == usbdev) {
			usbmuxd_log(LL_NOTICE, "Removed device %d on location 0x%x", dev->id, usb_get_location(usbdev));
			if(dev->state == MUXDEV_ACTIVE) {
				dev->state = MUXDEV_DEAD;
				FOREACH(struct mux_connection *conn, &dev->connections) {
					connection_teardown(conn);
				} ENDFOREACH
				client_device_remove(dev->id);
				collection_free(&dev->connections);
			}
			collection_remove(&device_list, dev);
			free(dev->pktbuf);
			free(dev);
			return;
		}
	} ENDFOREACH
	usbmuxd_log(LL_WARNING, "Cannot find device entry while removing USB device %p on location 0x%x", usbdev, usb_get_location(usbdev));
}

int device_get_count(void)
{
	int count = 0;
	FOREACH(struct mux_device *dev, &device_list) {
		if(dev->state == MUXDEV_ACTIVE)
			count++;
	} ENDFOREACH
	return count;
}

int device_get_list(struct device_info *p)
{
	int count = 0;
	FOREACH(struct mux_device *dev, &device_list) {
		if(dev->state == MUXDEV_ACTIVE) {
			p->id = dev->id;
			p->serial = usb_get_serial(dev->usbdev);
			p->location = usb_get_location(dev->usbdev);
			p->pid = usb_get_pid(dev->usbdev);
			count++;
			p++;
		}
	} ENDFOREACH
	return count;
}

int device_get_timeout(void)
{
	uint64_t oldest = (uint64_t)-1;
	FOREACH(struct mux_device *dev, &device_list) {
		if(dev->state == MUXDEV_ACTIVE) {
			FOREACH(struct mux_connection *conn, &dev->connections) {
				if((conn->state == CONN_CONNECTED) && (conn->flags & CONN_ACK_PENDING) && conn->last_ack_time < oldest)
					oldest = conn->last_ack_time;
			} ENDFOREACH
		}
	} ENDFOREACH
	uint64_t ct = mstime64();
	if(oldest == -1)
		return 100000; //meh
	if((ct - oldest) > ACK_TIMEOUT)
		return 0;
	return ACK_TIMEOUT - (ct - oldest);
}

void device_check_timeouts(void)
{
	uint64_t ct = mstime64();
	FOREACH(struct mux_device *dev, &device_list) {
		if(dev->state == MUXDEV_ACTIVE) {
			FOREACH(struct mux_connection *conn, &dev->connections) {
				if((conn->state == CONN_CONNECTED) && 
						(conn->flags & CONN_ACK_PENDING) && 
						(ct - conn->last_ack_time) > ACK_TIMEOUT) {
					usbmuxd_log(LL_DEBUG, "Sending ACK due to expired timeout (%" PRIu64 " -> %" PRIu64 ")", conn->last_ack_time, ct);
					if(send_tcp(conn, TH_ACK, NULL, 0) < 0) {
						usbmuxd_log(LL_ERROR, "Error sending TCP ACK to device %d (%d->%d)", dev->id, conn->sport, conn->dport);
						connection_teardown(conn);
					}
				}
			} ENDFOREACH
		}
	} ENDFOREACH
}

void device_init(void)
{
	usbmuxd_log(LL_DEBUG, "device_init");
	collection_init(&device_list);
	next_device_id = 1;
}

void device_kill_connections(void)
{
	usbmuxd_log(LL_DEBUG, "device_kill_connections");
	FOREACH(struct mux_device *dev, &device_list) {
		if(dev->state != MUXDEV_INIT) {
			FOREACH(struct mux_connection *conn, &dev->connections) {
				connection_teardown(conn);
			} ENDFOREACH
		}
	} ENDFOREACH
	// give USB a while to send the final connection RSTs and the like
	usb_process_timeout(100);
}

void device_shutdown(void)
{
	usbmuxd_log(LL_DEBUG, "device_shutdown");
	FOREACH(struct mux_device *dev, &device_list) {
		FOREACH(struct mux_connection *conn, &dev->connections) {
			connection_teardown(conn);
		} ENDFOREACH
		collection_free(&dev->connections);
		collection_remove(&device_list, dev);
		free(dev);
	} ENDFOREACH
	collection_free(&device_list);
}
