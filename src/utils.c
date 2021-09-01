/*
 * utils.c
 *
 * Copyright (C) 2009 Hector Martin <hector@marcansoft.com>
 * Copyright (C) 2009 Nikias Bassen <nikias@gmx.li>
 * Copyright (c) 2013 Federico Mena Quintero
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#include "utils.h"

#include "log.h"
#define util_error(...) usbmuxd_log(LL_ERROR, __VA_ARGS__)

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

#ifndef HAVE_CLOCK_GETTIME
typedef int clockid_t;
#define CLOCK_MONOTONIC 1

static int clock_gettime(clockid_t clk_id, struct timespec *ts)
{
	// See http://developer.apple.com/library/mac/qa/qa1398

	uint64_t mach_time, nano_sec;

	static mach_timebase_info_data_t base_info;

	mach_time = mach_absolute_time();

	if (base_info.denom == 0) {
		(void) mach_timebase_info(&base_info);
	}

	if (base_info.numer == 1 && base_info.denom == 1)
		nano_sec = mach_time;
	else
		nano_sec = mach_time * base_info.numer / base_info.denom;

	ts->tv_sec = nano_sec / 1000000000;
	ts->tv_nsec = nano_sec % 1000000000;

	return 0;
}
#endif

void get_tick_count(struct timeval * tv)
{
	struct timespec ts;
	if(0 == clock_gettime(CLOCK_MONOTONIC, &ts)) {
		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / 1000;
	} else {
		gettimeofday(tv, NULL);
	}
}

/**
 * Get number of milliseconds since the epoch.
 */
uint64_t mstime64(void)
{
	struct timeval tv;
	get_tick_count(&tv);

	// Careful, avoid overflow on 32 bit systems
	// time_t could be 4 bytes
	return ((long long)tv.tv_sec) * 1000LL + ((long long)tv.tv_usec) / 1000LL;
}
