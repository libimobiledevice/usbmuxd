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

#define _BSD_SOURCE

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "log.h"
#include "usb.h"

const char *socket_path = "/tmp/usbmuxd"; //TODO: CHANGEME

int create_socket(void) {
	struct sockaddr_un bind_addr;
	int listenfd;

	if(unlink(socket_path) == -1 && errno != ENOENT) {
		usbmuxd_log(LL_FATAL, "unlink(%s) failed: %s", socket_path, strerror(errno));
		return -1;
	}

	listenfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listenfd == -1) {
		usbmuxd_log(LL_FATAL, "socket() failed: %s", strerror(errno));
		return -1;
	}

	bzero(&bind_addr, sizeof(bind_addr));
	bind_addr.sun_family = AF_UNIX;
	strcpy(bind_addr.sun_path, socket_path);
	if (bind(listenfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) != 0) {
		usbmuxd_log(LL_FATAL, "bind() failed: %s", strerror(errno));
		return -1;
	}

	// Start listening
	if (listen(listenfd, 5) != 0) {
		usbmuxd_log(LL_FATAL, "listen() failed: %s", strerror(errno));
		return -1;
	}

	return listenfd;
}

int main_loop(int listenfd)
{
	int to, cnt, i;
	struct fdlist pollfds;
	
	while(1) {
		usbmuxd_log(LL_SPEW, "main_loop iteration");
		to = usb_get_timeout();
		usbmuxd_log(LL_SPEW, "USB timeout is %d ms", to);
		
		fdlist_create(&pollfds);
		usb_get_fds(&pollfds);
		usbmuxd_log(LL_SPEW, "fd count is %d", pollfds.count);
		
		cnt = poll(pollfds.fds, pollfds.count, to);
		usbmuxd_log(LL_SPEW, "poll() returned %d", cnt);
		
		if(cnt == 0) {
			if(usb_process() < 0) {
				usbmuxd_log(LL_FATAL, "usb_process() failed");
				return -1;
			}
		} else {
			for(i=0; i<pollfds.count; i++) {
				if(pollfds.fds[i].revents) {
					if(pollfds.owners[i] == FD_USB) {
						if(usb_process() < 0) {
							usbmuxd_log(LL_FATAL, "usb_process() failed");
							return -1;
						}
					}
				}
			}
		}
		fdlist_free(&pollfds);
	}
}

int main(int argc, char *argv[])
{
	int listenfd;
	int res;

	usbmuxd_log(LL_NOTICE, "usbmux v0.1 starting up");

	usbmuxd_log(LL_INFO, "Creating socket");
	listenfd = create_socket();
	if(listenfd < 0)
		return 1;

	usbmuxd_log(LL_INFO, "Initializing USB");
	if((res = usb_init()) < 0)
		return 2;
	usbmuxd_log(LL_INFO, "%d device%s detected", res, (res==1)?"":"s");
	
	usbmuxd_log(LL_NOTICE, "Initialization complete");

	res = main_loop(listenfd);
	if(res < 0)
		usbmuxd_log(LL_FATAL, "main_loop failed");

	usbmuxd_log(LL_NOTICE, "usbmux shutting down");
	usb_shutdown();
	usbmuxd_log(LL_NOTICE, "Shutdown complete");
	
	if(res < 0)
		return -res;
	return 0;
}
