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

#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef USBMUXD_DAEMON
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
void fdlist_reset(struct fdlist *list);
#endif

struct collection {
	void **list;
	int capacity;
};

void collection_init(struct collection *col);
void collection_add(struct collection *col, void *element);
void collection_remove(struct collection *col, void *element);
int collection_count(struct collection *col);
void collection_free(struct collection *col);

#define FOREACH(var, col) \
	do { \
		int _iter; \
		for(_iter=0; _iter<(col)->capacity; _iter++) { \
			if(!(col)->list[_iter]) continue; \
			var = (col)->list[_iter];

#define ENDFOREACH \
		} \
	} while(0);

#endif
