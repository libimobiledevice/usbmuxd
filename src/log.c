/*
 * log.c
 *
 * Copyright (C) 2009 Hector Martin <hector@marcansoft.com>
 * Copyright (C) 2009 Nikias Bassen <nikias@gmx.li>
 * Copyright (C) 2014 Frederik Carlier <frederik.carlier@quamotion.mobi>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <time.h>
#include <sys/time.h>

#ifdef WIN32
#else
#include <syslog.h>
#endif

#include "log.h"
#include "utils.h"

unsigned int log_level = LL_WARNING;

#ifdef WIN32
#else
int log_syslog = 0;

void log_enable_syslog()
{
	if (!log_syslog) {
		openlog("usbmuxd", LOG_PID, 0);
		log_syslog = 1;
	}
}

void log_disable_syslog()
{
	if (log_syslog) {
		closelog();
	}
}

static int level_to_syslog_level(int level)
{
	int result = level + LOG_CRIT;
	if (result > LOG_DEBUG) {
		result = LOG_DEBUG;
	}
	return result;
}
#endif

void usbmuxd_log(enum loglevel level, const char *fmt, ...)
{
	va_list ap;
	char *fs;
	struct timeval ts;
	struct tm *tp;

	if(level > log_level)
		return;

	get_tick_count(&ts);

	fs = malloc(20 + strlen(fmt));

#ifdef WIN32
	time_t ltime;
	time(&ltime);
	tp = localtime(&ltime);

	strftime(fs, 10, "[%H:%M:%S", tp);
	sprintf(fs + 9, ".%03d][%d] %s\n", (int)(ts.tv_usec / 1000), level, fmt);
#else
	tp = localtime(&ts.tv_sec);

	if(log_syslog) {
		sprintf(fs, "[%d] %s\n", level, fmt);
	} else {
		strftime(fs, 10, "[%H:%M:%S", tp);
		sprintf(fs+9, ".%03d][%d] %s\n", (int)(ts.tv_usec / 1000), level, fmt);
	}
#endif

	va_start(ap, fmt);

#ifdef WIN32
	vfprintf(stderr, fs, ap);
#else
	if (log_syslog) {
		vsyslog(level_to_syslog_level(level), fs, ap);
	} else {
		vfprintf(stderr, fs, ap);
	}
#endif

	va_end(ap);

	free(fs);
}
