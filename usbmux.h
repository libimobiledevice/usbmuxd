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

#ifndef __USBMUX_H__
#define __USBMUX_H__

#include <stdint.h>
#include <sys/types.h>
//#include <sys/stat.h>


void usbmux_set_debug(int e);

struct usbmux_device_int;
typedef struct usbmux_device_int *usbmux_device_t;

struct usbmux_client_int;
typedef struct usbmux_client_int *usbmux_client_t;

int usbmux_get_device ( usbmux_device_t *device );
int usbmux_get_specific_device(int bus_n, int dev_n, usbmux_device_t * device);
int usbmux_free_device ( usbmux_device_t device );


int usbmux_new_client ( usbmux_device_t device, uint16_t src_port, uint16_t dst_port, usbmux_client_t *client );
int usbmux_free_client ( usbmux_client_t client );

int usbmux_send(usbmux_client_t client, const char *data, uint32_t datalen, uint32_t * sent_bytes);

int usbmux_recv_timeout(usbmux_client_t client, char *data, uint32_t datalen, uint32_t * recv_bytes, int timeout);

int usbmux_pullbulk(usbmux_device_t device);

int usbmux_get_error(usbmux_client_t client);

#endif
