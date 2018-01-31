/*
 * winsock2-ext.h
 *
 * Copyright (C) 2016 Frederik Carlier <frederik.carlier@quamotion.mobi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 or version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define nfds_t ULONG

#define POLLRDNORM 0x0100
#define POLLRDBAND 0x0200
#define POLLIN    (POLLRDNORM | POLLRDBAND)

#define POLLWRNORM 0x0010
#define POLLOUT (POLLWRNORM)

/* Mapping of BSD names to Windows names */
#define EWOULDBLOCK WSAEWOULDBLOCK
#define sockaddr_un sockaddr_in