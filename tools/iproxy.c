/*
	iproxy -- proxy that enables tcp service access to iPhone/iPod

Copyright (C) 2009	Nikias Bassen <nikias@gmx.li>
Copyright (C) 2009	Paul Sladen <libiphone@paul.sladen.org>

Based upon iTunnel source code, Copyright (c) 2008 Jing Su.
http://www.cs.toronto.edu/~jingsu/itunnel/

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

TODO: improve code...

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#ifdef WIN32
#include <windows.h>
#include <winsock2.h>
typedef unsigned int socklen_t;
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#endif
#include <pthread.h>
#include <netinet/in.h>
#include "sock_stuff.h"
#include "usbmuxd.h"

static uint16_t listen_port = 0;
static uint16_t device_port = 0;

struct client_data {
    int fd;
    int sfd;
    volatile int stop_ctos;
    volatile int stop_stoc;
};

void *run_stoc_loop(void *arg)
{
    struct client_data *cdata = (struct client_data*)arg;
    int recv_len;
    int sent;
    char buffer[131072];

    printf("%s: fd = %d\n", __func__, cdata->fd);

    while (!cdata->stop_stoc && cdata->fd>0 && cdata->sfd>0) {
	recv_len = recv_buf_timeout(cdata->sfd, buffer, sizeof(buffer), 0, 5000);
	if (recv_len <= 0) {
	    if (recv_len == 0) {
		// try again
		continue;
	    } else {
		fprintf(stderr, "recv failed: %s\n", strerror(errno));
		break;
	    }
	} else {
//	    printf("received %d bytes from server\n", recv_len);
	    // send to socket
	    sent = send_buf(cdata->fd, buffer, recv_len);
	    if (sent < recv_len) {
		if (sent <= 0) {
		    fprintf(stderr, "send failed: %s\n", strerror(errno));
		    break;
		} else {
		    fprintf(stderr, "only sent %d from %d bytes\n", sent, recv_len);
		}
	    } else {
		// sending succeeded, receive from device
//		printf("pushed %d bytes to client\n", sent);
	    }
	}
    }
    close(cdata->fd);
    cdata->fd = -1;
    cdata->stop_ctos = 1;

    return NULL;
}

void *run_ctos_loop(void *arg)
{
    struct client_data *cdata = (struct client_data*)arg;
    int recv_len;
    int sent;
    char buffer[131072];
    pthread_t stoc;

    printf("%s: fd = %d\n", __func__, cdata->fd);

    cdata->stop_stoc = 0;
    pthread_create(&stoc, NULL, run_stoc_loop, cdata);

    while (!cdata->stop_ctos && cdata->fd>0 && cdata->sfd>0) {
	recv_len = recv_buf_timeout(cdata->fd, buffer, sizeof(buffer), 0, 5000);
	if (recv_len <= 0) {
	    if (recv_len == 0) {
		// try again
		continue;
	    } else {
		fprintf(stderr, "recv failed: %s\n", strerror(errno));
		break;
	    }
	} else {
//	    printf("pulled %d bytes from client\n", recv_len);
	    // send to local socket
	    sent = send_buf(cdata->sfd, buffer, recv_len);
	    if (sent < recv_len) {
		if (sent <= 0) {
		    fprintf(stderr, "send failed: %s\n", strerror(errno));
		    break;
		} else {
		    fprintf(stderr, "only sent %d from %d bytes\n", sent, recv_len);
		}
	    } else {
		// sending succeeded, receive from device
//		printf("sent %d bytes to server\n", sent);
	    }
	}
    }
    close(cdata->fd);
    cdata->fd = -1;
    cdata->stop_stoc = 1;

    pthread_join(stoc, NULL);

    return NULL;
}

void *acceptor_thread(void *arg)
{
    struct client_data *cdata;
    usbmuxd_device_info_t *dev_list = NULL;
    pthread_t ctos;
    int count;

    if (!arg) {
	fprintf(stderr, "invalid client_data provided!\n");
	return NULL;
    }

    cdata = (struct client_data*)arg;

    if ((count = usbmuxd_get_device_list(&dev_list)) < 0) {
	printf("Connecting to usbmuxd failed, terminating.\n");
	free(dev_list);
	return NULL;
    }

    fprintf(stdout, "Number of available devices == %d\n", count);

    if (dev_list == NULL || dev_list[0].handle == 0) {
	printf("No connected device found, terminating.\n");
	free(dev_list);
	return NULL;
    }

    fprintf(stdout, "Requesting connecion to device handle == %d (serial: %s), port %d\n", dev_list[0].handle, dev_list[0].uuid, device_port);

    cdata->sfd = usbmuxd_connect(dev_list[0].handle, device_port);
    free(dev_list);
    if (cdata->sfd < 0) {
    	fprintf(stderr, "Error connecting to device!\n");
    } else {
	cdata->stop_ctos = 0;
	pthread_create(&ctos, NULL, run_ctos_loop, cdata);
	pthread_join(ctos, NULL);
    }

    if (cdata->fd > 0) {
	close(cdata->fd);
    }
    if (cdata->sfd > 0) {
	close(cdata->sfd);
    }

    return NULL;
}

int main(int argc, char **argv)
{
    int mysock = -1;

    if (argc != 3) {
	printf("usage: %s LOCAL_TCP_PORT DEVICE_TCP_PORT\n", argv[0]);
	return 0;
    }

    listen_port = atoi(argv[1]);
    device_port = atoi(argv[2]);

    if (!listen_port) {
	fprintf(stderr, "Invalid listen_port specified!\n");
	return -EINVAL;
    }

    if (!device_port) {
	fprintf(stderr, "Invalid device_port specified!\n");
	return -EINVAL;
    }

    // first create the listening socket endpoint waiting for connections.
    mysock = create_socket(listen_port);
    if (mysock < 0) {
	fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
	return -errno;
    } else {
	pthread_t acceptor;
	struct sockaddr_in c_addr;
	socklen_t len = sizeof(struct sockaddr_in);
	struct client_data cdata;
	int c_sock;
	while (1) {
	    printf("waiting for connection\n");
	    c_sock = accept(mysock, (struct sockaddr*)&c_addr, &len);
	    if (c_sock) {
		printf("accepted connection, fd = %d\n", c_sock);
		cdata.fd = c_sock;
		pthread_create(&acceptor, NULL, acceptor_thread, &cdata);
		pthread_join(acceptor, NULL);
	    } else {
		break;
	    }
	}
	close(c_sock);
	close(mysock);
    }

    return 0;
}
