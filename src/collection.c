/*
	libusbmuxd - client library to talk to usbmuxd

Copyright (C) 2009	Hector Martin "marcan" <hector@marcansoft.com>
Copyright (C) 2009	Nikias Bassen <nikias@gmx.li>

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "collection.h"

#ifdef _MSC_VER
	#define __func__ __FUNCTION__
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
	fprintf(stderr, "%s: WARNING: element %p not present in collection %p (cap %d)", __func__, element, col, col->capacity);
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
