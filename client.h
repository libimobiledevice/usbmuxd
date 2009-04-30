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
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef __CLIENT_H__
#define __CLIENT_H__

#include <stdint.h>

struct device_info;
struct mux_client;

enum client_result {
	RESULT_OK = 0,
	RESULT_BADCOMMAND = 1,
	RESULT_BADDEV = 2,
	RESULT_CONNREFUSED = 3,
	// ???
	// ???
	RESULT_BADVERSION = 6,
};

enum client_msgtype {
	MESSAGE_RESULT = 1,
	MESSAGE_CONNECT = 2,
	MESSAGE_LISTEN = 3,
	MESSAGE_DEVICE_ADD = 4,
	MESSAGE_DEVICE_REMOVE = 5,
	//???
	//???
	//MESSAGE_PLIST = 8,
};

#define CLIENT_PROTOCOL_VERSION 0

struct client_header {
	uint32_t length;
	uint32_t version;
	uint32_t message;
	uint32_t tag;
};

struct client_msg_result {
	uint32_t result;
};

struct client_msg_connect {
	uint32_t device_id;
	uint16_t port;
};

struct client_msg_dev {
	uint32_t device_id;
	uint16_t device_pid;
	char device_serial[256];
	uint16_t padding;
	uint32_t location;
};

int client_read(struct mux_client *client, void *buffer, int len);
int client_write(struct mux_client *client, void *buffer, int len);
int client_set_events(struct mux_client *client, short events);
void client_close(struct mux_client *client);
int client_notify_connect(struct mux_client *client, enum client_result result);

void client_device_add(struct device_info *dev);
void client_device_remove(int device_id);

int client_accept(int fd);
void client_get_fds(struct fdlist *list);
void client_process(int fd, short events);

void client_init(void);
void client_shutdown(void);

#endif
