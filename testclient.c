#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include "usbmuxd.h"
#include "sock_stuff.h"

int usbmuxd_get_result(int sfd, uint32_t tag, uint32_t *result)
{
    struct usbmuxd_result res;
    int recv_len;

    if (!result) {
	return -EINVAL;
    }

    if ((recv_len = recv_buf(sfd, &res, sizeof(res))) <= 0) {
	perror("recv");
	return -errno;
    } else {
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

int main(int argc, char **argv)
{
    int sfd;
    int recv_len = 0;
    int scan_done;
    int connected;
    uint32_t pktlen;
    unsigned char *buf;
    struct usbmuxd_scan_request scan;
    struct am_device_info device_info;

    sfd = connect_unix_socket(USBMUXD_SOCKET_FILE);
    if (sfd < 0) {
	printf("error opening socket, terminating.\n");
	return -1;
    }

    // send scan
    scan.header.length = sizeof(struct usbmuxd_scan_request);
    scan.header.reserved = 0;
    scan.header.type = USBMUXD_SCAN;
    scan.header.tag = 2;

    scan_done = 0;
    connected = 0;

    fprintf(stdout, "sending scan packet\n");
    if (send(sfd, &scan, scan.header.length, 0) == scan.header.length) {
	uint32_t res = -1;
	// get response
	if (usbmuxd_get_result(sfd, scan.header.tag, &res) && (res==0)) {
	    fprintf(stdout, "Got response to scan request!\n");
	    scan_done = 1;
	} else {
	    fprintf(stderr, "Did not get response to scan request (with result=0)...\n");
	    close(sfd);
	    return -1;
	}

	device_info.device_id = 0;

	if (scan_done) {
	    // get all devices
	    while (1) {
		if (recv_buf_timeout(sfd, &pktlen, 4, MSG_PEEK, 1000) == 4) {
		    buf = (unsigned char*)malloc(pktlen);
		    if (!buf) {
			exit(-ENOMEM);
		    }
		    recv_len = recv_buf(sfd, buf, pktlen);
		    if (recv_len < pktlen) {
			fprintf(stdout, "received less data than specified in header!\n");
		    }
		    fprintf(stdout, "got device data:\n");
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

	    // try to connect to last device found
	    c_req.header.length = sizeof(c_req);
	    c_req.header.reserved = 0;
	    c_req.header.type = USBMUXD_CONNECT;
	    c_req.header.tag = 3;
	    c_req.device_id = device_info.device_id;
	    c_req.tcp_dport = htons(22);
	    c_req.reserved = 0;

	    if (send_buf(sfd, &c_req, sizeof(c_req)) < 0) {
		perror("send");
	    } else {
		// read ACK
		res = -1;
		if (usbmuxd_get_result(sfd, c_req.header.tag, &res)) {
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
	    

	    // do communication now.
	    sleep(10);
	}
    }
    close(sfd);

    return 0;
}
