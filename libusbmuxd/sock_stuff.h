/*
	libusbmuxd - client library to talk to usbmuxd

Copyright (C) 2009	Nikias Bassen <nikias@gmx.li>
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

#ifndef __SOCK_STUFF_H
#define __SOCK_STUFF_H

#include <stdint.h>

enum fd_mode {
	FDM_READ,
	FDM_WRITE,
	FDM_EXCEPT
};
typedef enum fd_mode fd_mode;

#ifndef WIN32
int create_unix_socket(const char *filename);
int connect_unix_socket(const char *filename);
#endif
int create_socket(uint16_t port);
#if defined(WIN32) || defined(__CYGWIN__)
int connect_socket(const char *addr, uint16_t port);
#endif
int check_fd(int fd, fd_mode fdm, unsigned int timeout);

int close_socket(int fd);

int recv_buf(int fd, void *data, size_t size);
int peek_buf(int fd, void *data, size_t size);
int recv_buf_timeout(int fd, void *data, size_t size, int flags,
					 unsigned int timeout);

int send_buf(int fd, void *data, size_t size);

void sock_stuff_set_verbose(int level);

#endif							/* __SOCK_STUFF_H */
