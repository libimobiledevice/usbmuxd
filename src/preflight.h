/*
 * preflight.h
 *
 * Copyright (C) 2013 Nikias Bassen <nikias@gmx.li>
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

#ifndef PREFLIGHT_H
#define PREFLIGHT_H

#include "device.h"

extern void userpref_get_system_buid(char **systembuid);
extern void userpref_device_record_get_host_id(const char *udid, char **host_id);

void preflight_device_remove_cb(void *data);
void preflight_worker_device_add(struct device_info* info);

#endif
