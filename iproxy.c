/*
 * iproxy -- proxy that enables tcp service access to iPhone/iPod
 *  via USB cable
 * TODO: improve code...
 * 
 * Copyright (c) 2009 Nikias Bassen. All Rights Reserved.
 * Based upon iTunnel source code, Copyright (c) 2008 Jing Su.
 *  http://www.cs.toronto.edu/~jingsu/itunnel/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "usbmuxd.h"
#include "sock_stuff.h"

static uint16_t listen_port = 0;
static uint16_t device_port = 0;

pthread_mutex_t smutex = PTHREAD_MUTEX_INITIALIZER;

struct client_data {
    int fd;
    int sfd;
    volatile int stop_ctos;
    volatile int stop_stoc;
};

int usbmuxd_get_result(int sfd, uint32_t tag, uint32_t *result)
{
    struct usbmuxd_result res;
    int recv_len;
    int i;
    uint32_t rrr[5];

    if (!result) {
	return -EINVAL;
    }

    if ((recv_len = recv_buf(sfd, &res, sizeof(res))) <= 0) {
	perror("recv");
	return -errno;
    } else {
	memcpy(&rrr, &res, recv_len);
	for (i = 0; i < recv_len/4; i++) {
	    fprintf(stderr, "%08x ", rrr[i]);
	}
	fprintf(stderr, "\n");
	if ((recv_len == sizeof(res))
	    && (res.header.length == recv_len)
	    && (res.header.reserved == 0)
	    && (res.header.type == USBMUXD_RESULT)
	   ) {
	    *result = res.result;
	    if (res.header.tag == tag) {
		return 1;
	    } else {
		return 0;
	    }
        }
    }

    return -1;
}

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
	    printf("received %d bytes from server\n", recv_len);
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
		printf("pushed %d bytes to client\n", sent);
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
    pthread_t stoc = 0;

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
	    printf("pulled %d bytes from client\n", recv_len);
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
		printf("sent %d bytes to server\n", sent);
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
    int recv_len = 0;
    int scan_done;
    int connected;
    uint32_t pktlen;
    unsigned char *buf;
    struct usbmuxd_scan_request scan;
    struct am_device_info device_info;
    pthread_t ctos;

    if (!arg) {
	fprintf(stderr, "invalid client_data provided!\n");
	return NULL;
    }

    cdata = (struct client_data*)arg;

    cdata->sfd = connect_unix_socket(USBMUXD_SOCKET_FILE);
    if (cdata->sfd < 0) {
	printf("error opening socket, terminating.\n");
	return NULL;
    }

    // send scan
    scan.header.length = sizeof(struct usbmuxd_scan_request);
    scan.header.reserved = 0;
    scan.header.type = USBMUXD_SCAN;
    scan.header.tag = 2;

    scan_done = 0;
    connected = 0;

    fprintf(stdout, "sending scan packet\n");
    if (send(cdata->sfd, &scan, scan.header.length, 0) == scan.header.length) {
	uint32_t res = -1;
	// get response
	if (usbmuxd_get_result(cdata->sfd, scan.header.tag, &res) && (res==0)) {
	    fprintf(stdout, "Got response to scan request!\n");
	    scan_done = 1;
	} else {
	    fprintf(stderr, "Did not get response to scan request (with result=0)...\n");
	    close(cdata->sfd);
	    cdata->sfd = -1;
	    return NULL;
	}

	device_info.device_id = 0;

	if (scan_done) {
	    // get all devices
	    while (1) {
		if (recv_buf_timeout(cdata->sfd, &pktlen, 4, MSG_PEEK, 1000) == 4) {
		    buf = (unsigned char*)malloc(pktlen);
		    if (!buf) {
			exit(-ENOMEM);
		    }
		    recv_len = recv_buf(cdata->sfd, buf, pktlen);
		    if (recv_len < pktlen) {
			fprintf(stdout, "received less data than specified in header!\n");
		    }
		    fprintf(stdout, "Received device data\n");
		    //log_debug_buffer(stdout, (char*)buf, pktlen);
		    memcpy(&device_info, buf + sizeof(struct usbmuxd_header), sizeof(device_info));
		    free(buf);
		} else {
		    // we _should_ have all of them now.
		    // or perhaps an error occured.
		    break;
		}
	    }
	}

	if (device_info.device_id > 0) {
	    struct usbmuxd_connect_request c_req;

	    fprintf(stdout, "Requesting connecion to device %d port %d\n", device_info.device_id, device_port);

	    // try to connect to last device found
	    c_req.header.length = sizeof(c_req);
	    c_req.header.reserved = 0;
	    c_req.header.type = USBMUXD_CONNECT;
	    c_req.header.tag = 3;
	    c_req.device_id = device_info.device_id;
	    c_req.tcp_dport = htons(device_port);
	    c_req.reserved = 0;

	    if (send_buf(cdata->sfd, &c_req, sizeof(c_req)) < 0) {
		perror("send");
	    } else {
		// read ACK
		res = -1;
		fprintf(stdout, "Reading connect result...\n");
		if (usbmuxd_get_result(cdata->sfd, c_req.header.tag, &res)) {
		    if (res == 0) {
			fprintf(stdout, "Connect success!\n");
			connected = 1;
		    } else {
			fprintf(stderr, "Connect failed, Error code=%d\n", res);
		    }
		}
	    }
	}

	if (connected) {
		cdata->stop_ctos = 0;
		pthread_create(&ctos, NULL, run_ctos_loop, cdata);
		pthread_join(ctos, NULL);
	} else {
	    fprintf(stderr, "Error connecting to device!\n");
	}
    }
    close(cdata->fd);
    close(cdata->sfd);

    return NULL;
}

int main(int argc, char **argv)
{
    int mysock = -1;

    if (argc != 3) {
	printf("usage: %s LOCAL_PORT DEVICE_PORT\n", argv[0]);
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
