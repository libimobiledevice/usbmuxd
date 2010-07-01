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
#include <stdio.h>
#include "utils.h"

#ifdef USBMUXD_DAEMON
# include "log.h"
# define util_error(...) usbmuxd_log(LL_ERROR, __VA_ARGS__)
#else
# define util_error(...) fprintf(stderr, __VA_ARGS__)
#endif

#ifdef USBMUXD_DAEMON
void fdlist_create(struct fdlist *list)
{
	list->count = 0;
	list->capacity = 4;
	list->owners = malloc(sizeof(*list->owners) * list->capacity);
	list->fds = malloc(sizeof(*list->fds) * list->capacity);
}
void fdlist_add(struct fdlist *list, enum fdowner owner, int fd, short events)
{
	if(list->count == list->capacity) {
		list->capacity *= 2;
		list->owners = realloc(list->owners, sizeof(*list->owners) * list->capacity);
		list->fds = realloc(list->fds, sizeof(*list->fds) * list->capacity);
	}
	list->owners[list->count] = owner;
	list->fds[list->count].fd = fd;
	list->fds[list->count].events = events;
	list->fds[list->count].revents = 0;
	list->count++;
}

void fdlist_free(struct fdlist *list)
{
	list->count = 0;
	list->capacity = 0;
	free(list->owners);
	list->owners = NULL;
	free(list->fds);
	list->fds = NULL;
}

void fdlist_reset(struct fdlist *list)
{
	list->count = 0;
}
#endif

void collection_init(struct collection *col)
{
	col->list = malloc(sizeof(void *));
	memset(col->list, 0, sizeof(void *));
	col->capacity = 1;
}

void collection_free(struct collection *col)
{
	free(col->list);
	col->list = NULL;
	col->capacity = 0;
}

void collection_add(struct collection *col, void *element)
{
	int i;
	for(i=0; i<col->capacity; i++) {
		if(!col->list[i]) {
			col->list[i] = element;
			return;
		}
	}
	col->list = realloc(col->list, sizeof(void*) * col->capacity * 2);
	memset(&col->list[col->capacity], 0, sizeof(void *) * col->capacity);
	col->list[col->capacity] = element;
	col->capacity *= 2;
}

void collection_remove(struct collection *col, void *element)
{
	int i;
	for(i=0; i<col->capacity; i++) {
		if(col->list[i] == element) {
			col->list[i] = NULL;
			return;
		}
	}
	util_error("collection_remove: element %p not present in collection %p (cap %d)", element, col, col->capacity);
}

int collection_count(struct collection *col)
{
	int i, cnt = 0;
	for(i=0; i<col->capacity; i++) {
		if(col->list[i])
			cnt++;
	}
	return cnt;
}
