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

#ifndef __DEVICE_H__
#define __DEVICE_H__

#include "usb.h"
#include "client.h"

struct device_info {
	int id;
	const char *serial;
	uint32_t location;
	uint16_t pid;
};

void device_data_input(struct usb_device *dev, unsigned char *buf, uint32_t length);

int device_add(struct usb_device *dev);
void device_remove(struct usb_device *dev);

int device_start_connect(int device_id, uint16_t port, struct mux_client *client);
void device_client_process(int device_id, struct mux_client *client, short events);
void device_abort_connect(int device_id, struct mux_client *client);

void device_set_visible(int device_id);
void device_set_preflight_cb_data(int device_id, void* data);

int device_get_count(int include_hidden);
int device_get_list(int include_hidden, struct device_info *p);

int device_get_timeout(void);
void device_check_timeouts(void);

void device_init(void);
void device_kill_connections(void);
void device_shutdown(void);

#ifdef ANDROID
#include <endian.h>

typedef	u_int32_t tcp_seq;

/*
 * TCP header.
 * Per RFC 793, September, 1981.
 */
struct tcphdr_bsd {
	u_short	th_sport;		/* source port */
	u_short	th_dport;		/* destination port */
	tcp_seq	th_seq;			/* sequence number */
	tcp_seq	th_ack;			/* acknowledgement number */
	#if __BYTE_ORDER == __LITTLE_ENDIAN
	u_int	th_x2:4,		/* (unused) */
		th_off:4;		/* data offset */
	#endif
	#if __BYTE_ORDER == __BIG_ENDIAN
	u_int	th_off:4,		/* data offset */
		th_x2:4;		/* (unused) */
	#endif
	u_char	th_flags;
	#define	TH_FIN	0x01
	#define	TH_SYN	0x02
	#define	TH_RST	0x04
	#define	TH_PUSH	0x08
	#define	TH_ACK	0x10
	#define	TH_URG	0x20
	#define	TH_ECE	0x40
	#define	TH_CWR	0x80
	#define	TH_FLAGS	(TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)

	u_short	th_win;			/* window */
	u_short	th_sum;			/* checksum */
	u_short	th_urp;			/* urgent pointer */
};

#endif

#endif
