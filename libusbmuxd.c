#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

// usbmuxd public interface
#include <usbmuxd.h>
// usbmuxd protocol 
#include <usbmuxd-proto.h>
// socket utility functions
#include "sock_stuff.h"

static int usbmuxd_get_result(int sfd, uint32_t tag, uint32_t *result)
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

int usbmuxd_scan(usbmuxd_scan_result **available_devices)
{
	struct usbmuxd_scan_request s_req;
	int sfd;
	int scan_success = 0;
	uint32_t res;
	uint32_t pktlen;
	int recv_len;
	usbmuxd_scan_result *newlist = NULL;
	struct usbmuxd_device_info_record dev_info_pkt;
	int dev_cnt = 0;

	sfd = connect_unix_socket(USBMUXD_SOCKET_FILE);
	if (sfd < 0) {
		fprintf(stderr, "%s: error opening socket!\n", __func__);
		return sfd;
	}

	s_req.header.length = sizeof(struct usbmuxd_scan_request);
	s_req.header.reserved = 0;
	s_req.header.type = USBMUXD_SCAN;
	s_req.header.tag = 2;

	// send scan request packet
	if (send_buf(sfd, &s_req, s_req.header.length) == s_req.header.length) {
		res = -1;
		// get response
		if (usbmuxd_get_result(sfd, s_req.header.tag, &res) && (res == 0)) {
			scan_success = 1;
		} else {
			fprintf(stderr, "%s: Did not get response to scan request (with result=0)...\n", __func__);
			close(sfd);
			return res;
		}
	}

	if (!scan_success) {
		fprintf(stderr, "%s: Could not send scan request!\n", __func__);
		return -1;
	}

	*available_devices = NULL;
	// receive device list
	while (1) {
		if (recv_buf_timeout(sfd, &pktlen, 4, MSG_PEEK, 500) == 4) {
			if (pktlen != sizeof(dev_info_pkt)) {
				// invalid packet size received!
				fprintf(stderr, "%s: Invalid packet size (%d) received when expecting a device info record.\n", __func__, pktlen);
				break;
			}

			recv_len = recv_buf(sfd, &dev_info_pkt, pktlen);
			if (recv_len <= 0) {
				fprintf(stderr, "%s: Error when receiving device info record\n", __func__);
				break;
			} else if (recv_len < pktlen) {
				fprintf(stderr, "%s: received less data than specified in header!\n", __func__);
			} else {
				//fprintf(stderr, "%s: got device record with id %d, UUID=%s\n", __func__, dev_info_pkt.device_info.device_id, dev_info_pkt.device_info.serial_number);
				newlist = (usbmuxd_scan_result *)realloc(*available_devices, sizeof(usbmuxd_scan_result) * (dev_cnt+1));
				if (newlist) {
					newlist[dev_cnt].handle = (int)dev_info_pkt.device.device_id;
					newlist[dev_cnt].product_id = dev_info_pkt.device.product_id;
					memset(newlist[dev_cnt].serial_number, '\0', sizeof(newlist[dev_cnt].serial_number));
					memcpy(newlist[dev_cnt].serial_number, dev_info_pkt.device.serial_number,
					       sizeof(dev_info_pkt.device.serial_number));
					*available_devices = newlist;
					dev_cnt++;
				} else {
					fprintf(stderr, "%s: ERROR: out of memory when trying to realloc!\n", __func__);
					break;
				}
			}
		} else {
			// we _should_ have all of them now.
			// or perhaps an error occured.
			break;
		}
	}

	// terminating zero record
	newlist = (usbmuxd_scan_result *)realloc(*available_devices, sizeof(usbmuxd_scan_result) * (dev_cnt+1));
	memset(newlist+dev_cnt, 0, sizeof(usbmuxd_scan_result));
	*available_devices = newlist;

	return dev_cnt;
}

int usbmuxd_connect(const int handle, const unsigned short tcp_port)
{
	int sfd;
	struct usbmuxd_connect_request c_req;
	int connected = 0;
	uint32_t res = -1;

	sfd = connect_unix_socket(USBMUXD_SOCKET_FILE);
	if (sfd < 0) {
	    fprintf(stderr, "%s: Error: Connection to usbmuxd failed: %s\n", __func__, strerror(errno));
	    return sfd;
	}

	c_req.header.length = sizeof(c_req);
	c_req.header.reserved = 0;
	c_req.header.type = USBMUXD_CONNECT;
	c_req.header.tag = 3;
	c_req.device_id = (uint32_t)handle;
	c_req.tcp_dport = htons(tcp_port);
	c_req.reserved = 0;

	if (send_buf(sfd, &c_req, sizeof(c_req)) < 0) {
	    perror("send");
	} else {
	    // read ACK
	    //fprintf(stderr, "%s: Reading connect result...\n", __func__);
	    if (usbmuxd_get_result(sfd, c_req.header.tag, &res)) {
		if (res == 0) {
		    //fprintf(stderr, "%s: Connect success!\n", __func__);
		    connected = 1;
		} else {
		    fprintf(stderr, "%s: Connect failed, Error code=%d\n", __func__, res);
		}
	    }
	}

	if (connected) {
		return sfd;
	}

	close(sfd);
	
	return -1;
}
