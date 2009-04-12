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

#ifndef __IPHONE_H__
#define __IPHONE_H__

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>

//general errors
#define IPHONE_E_SUCCESS          0
#define IPHONE_E_INVALID_ARG     -1
#define IPHONE_E_UNKNOWN_ERROR   -2
#define IPHONE_E_NO_DEVICE       -3
#define IPHONE_E_TIMEOUT         -4
#define IPHONE_E_NOT_ENOUGH_DATA -5
#define IPHONE_E_BAD_HEADER      -6

//lockdownd specific error
#define IPHONE_E_INVALID_CONF    -7
#define IPHONE_E_PAIRING_FAILED  -8
#define IPHONE_E_SSL_ERROR       -9
#define IPHONE_E_PLIST_ERROR    -10
#define IPHONE_E_DICT_ERROR     -11

//afc specific error
#define IPHONE_E_NO_SUCH_FILE   -12

//general TCP-style errors and conditions
#define IPHONE_E_ECONNABORTED -ECONNABORTED
#define IPHONE_E_ECONNRESET   -ECONNRESET
#define IPHONE_E_ENOTCONN     -ENOTCONN
#define IPHONE_E_ESHUTDOWN    -ESHUTDOWN
#define IPHONE_E_ETIMEDOUT    -ETIMEDOUT
#define IPHONE_E_ECONNREFUSED -ECONNREFUSED

void iphone_set_debug(int e);

typedef int16_t iphone_error_t;

struct iphone_device_int;
typedef struct iphone_device_int *iphone_device_t;

struct iphone_umux_client_int;
typedef struct iphone_umux_client_int *iphone_umux_client_t;

iphone_error_t iphone_get_device ( iphone_device_t *device );
iphone_error_t iphone_get_specific_device(int bus_n, int dev_n, iphone_device_t * device);
iphone_error_t iphone_free_device ( iphone_device_t device );


iphone_error_t iphone_mux_new_client ( iphone_device_t device, uint16_t src_port, uint16_t dst_port, iphone_umux_client_t *client );
iphone_error_t iphone_mux_free_client ( iphone_umux_client_t client );

iphone_error_t iphone_mux_send(iphone_umux_client_t client, const char *data, uint32_t datalen, uint32_t * sent_bytes);

iphone_error_t iphone_mux_recv(iphone_umux_client_t client, char *data, uint32_t datalen, uint32_t * recv_bytes);
iphone_error_t iphone_mux_recv_timeout(iphone_umux_client_t client, char *data, uint32_t datalen, uint32_t * recv_bytes, int timeout);

int iphone_mux_pullbulk(iphone_device_t phone);

iphone_error_t iphone_mux_get_error(iphone_umux_client_t client);

#endif
