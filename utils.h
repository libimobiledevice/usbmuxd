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

#ifndef __UTILS_H__
#define __UTILS_H__

#include <poll.h>

enum fdowner {
	FD_LISTEN,
	FD_CLIENT,
	FD_USB
};

struct fdlist {
	int count;
	int capacity;
	enum fdowner *owners;
	struct pollfd *fds;
};

void fdlist_create(struct fdlist *list);
void fdlist_add(struct fdlist *list, enum fdowner owner, int fd, short events);
void fdlist_free(struct fdlist *list);

#define MIN(X, Y)  ((X) < (Y) ? (X) : (Y))

#endif
