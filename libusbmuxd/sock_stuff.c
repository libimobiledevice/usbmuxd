/*
	libusbmuxd - client library to talk to usbmuxd

Copyright (C) 2009	Nikias Bassen <nikias@gmx.li>
Copyright (C) 2009	Paul Sladen <libiphone@paul.sladen.org>
Copyright (C) 2009	Martin Szulecki <opensuse@sukimashita.com>

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

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#ifdef WIN32
#include <windows.h>
#include <winsock2.h>
static int wsa_init = 0;
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif
#include "sock_stuff.h"

#define RECV_TIMEOUT 20000

static int verbose = 0;

void sock_stuff_set_verbose(int level)
{
	verbose = level;
}

#ifndef WIN32
int create_unix_socket(const char *filename)
{
	struct sockaddr_un name;
	int sock;
	size_t size;

	// remove if still present
	unlink(filename);

	/* Create the socket. */
	sock = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("socket");
		return -1;
	}

	/* Bind a name to the socket. */
	name.sun_family = AF_LOCAL;
	strncpy(name.sun_path, filename, sizeof(name.sun_path));
	name.sun_path[sizeof(name.sun_path) - 1] = '\0';

	/* The size of the address is
	   the offset of the start of the filename,
	   plus its length,
	   plus one for the terminating null byte.
	   Alternatively you can just do:
	   size = SUN_LEN (&name);
	 */
	size = (offsetof(struct sockaddr_un, sun_path)
			+ strlen(name.sun_path) + 1);

	if (bind(sock, (struct sockaddr *) &name, size) < 0) {
		perror("bind");
		close_socket(sock);
		return -1;
	}

	if (listen(sock, 10) < 0) {
		perror("listen");
		close_socket(sock);
		return -1;
	}

	return sock;
}

int connect_unix_socket(const char *filename)
{
	struct sockaddr_un name;
	int sfd = -1;
	size_t size;
	struct stat fst;

	// check if socket file exists...
	if (stat(filename, &fst) != 0) {
		if (verbose >= 2)
			fprintf(stderr, "%s: stat '%s': %s\n", __func__, filename,
					strerror(errno));
		return -1;
	}
	// ... and if it is a unix domain socket
	if (!S_ISSOCK(fst.st_mode)) {
		if (verbose >= 2)
			fprintf(stderr, "%s: File '%s' is not a socket!\n", __func__,
					filename);
		return -1;
	}
	// make a new socket
	if ((sfd = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
		if (verbose >= 2)
			fprintf(stderr, "%s: socket: %s\n", __func__, strerror(errno));
		return -1;
	}
	// and connect to 'filename'
	name.sun_family = AF_LOCAL;
	strncpy(name.sun_path, filename, sizeof(name.sun_path));
	name.sun_path[sizeof(name.sun_path) - 1] = 0;

	size = (offsetof(struct sockaddr_un, sun_path)
			+ strlen(name.sun_path) + 1);

	if (connect(sfd, (struct sockaddr *) &name, size) < 0) {
		close_socket(sfd);
		if (verbose >= 2)
			fprintf(stderr, "%s: connect: %s\n", __func__,
					strerror(errno));
		return -1;
	}

	return sfd;
}
#endif

int create_socket(uint16_t port)
{
	int sfd = -1;
	int yes = 1;
#ifdef WIN32
	WSADATA wsa_data;
	if (!wsa_init) {
		if (WSAStartup(MAKEWORD(2,2), &wsa_data) != ERROR_SUCCESS) {
			fprintf(stderr, "WSAStartup failed!\n");
			ExitProcess(-1);
		}
		wsa_init = 1;
	}
#endif
	struct sockaddr_in saddr;

	if (0 > (sfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP))) {
		perror("socket()");
		return -1;
	}

	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void*)&yes, sizeof(int)) == -1) {
		perror("setsockopt()");
		close_socket(sfd);
		return -1;
	}

	memset((void *) &saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr.sin_port = htons(port);

	if (0 > bind(sfd, (struct sockaddr *) &saddr, sizeof(saddr))) {
		perror("bind()");
		close_socket(sfd);
		return -1;
	}

	if (listen(sfd, 1) == -1) {
		perror("listen()");
		close_socket(sfd);
		return -1;
	}

	return sfd;
}

#if defined(WIN32) || defined(__CYGWIN__)
int connect_socket(const char *addr, uint16_t port)
{
	int sfd = -1;
	int yes = 1;
	struct hostent *hp;
	struct sockaddr_in saddr;
#ifdef WIN32
	WSADATA wsa_data;
	if (!wsa_init) {
		if (WSAStartup(MAKEWORD(2,2), &wsa_data) != ERROR_SUCCESS) {
			fprintf(stderr, "WSAStartup failed!\n");
			ExitProcess(-1);
		}
		wsa_init = 1;
	}
#endif

	if (!addr) {
		errno = EINVAL;
		return -1;
	}

	if ((hp = gethostbyname(addr)) == NULL) {
		if (verbose >= 2)
			fprintf(stderr, "%s: unknown host '%s'\n", __func__, addr);
		return -1;
	}

	if (!hp->h_addr) {
		if (verbose >= 2)
			fprintf(stderr, "%s: gethostbyname returned NULL address!\n",
					__func__);
		return -1;
	}

	if (0 > (sfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP))) {
		perror("socket()");
		return -1;
	}

	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void*)&yes, sizeof(int)) == -1) {
		perror("setsockopt()");
		close_socket(sfd);
		return -1;
	}

	memset((void *) &saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = *(uint32_t *) hp->h_addr;
	saddr.sin_port = htons(port);

	if (connect(sfd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
		perror("connect");
		close_socket(sfd);
		return -2;
	}

	return sfd;
}
#endif /* WIN32 || __CYGWIN__ */

int check_fd(int fd, fd_mode fdm, unsigned int timeout)
{
	fd_set fds;
	int sret;
	int eagain;
	struct timeval to;
	struct timeval *pto;

	if (fd <= 0) {
		if (verbose >= 2)
			fprintf(stderr, "ERROR: invalid fd in check_fd %d\n", fd);
		return -1;
	}

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	if (timeout > 0) {
		to.tv_sec = (time_t) (timeout / 1000);
		to.tv_usec = (time_t) ((timeout - (to.tv_sec * 1000)) * 1000);
		pto = &to;
	} else {
		pto = NULL;
	}

	sret = -1;

	do {
		eagain = 0;
		switch (fdm) {
		case FDM_READ:
			sret = select(fd + 1, &fds, NULL, NULL, pto);
			break;
		case FDM_WRITE:
			sret = select(fd + 1, NULL, &fds, NULL, pto);
			break;
		case FDM_EXCEPT:
			sret = select(fd + 1, NULL, NULL, &fds, pto);
			break;
		default:
			return -1;
		}

		if (sret < 0) {
			switch (errno) {
			case EINTR:
				// interrupt signal in select
				if (verbose >= 2)
					fprintf(stderr, "%s: EINTR\n", __func__);
				eagain = 1;
				break;
			case EAGAIN:
				if (verbose >= 2)
					fprintf(stderr, "%s: EAGAIN\n", __func__);
				break;
			default:
				if (verbose >= 2)
					fprintf(stderr, "%s: select failed: %s\n", __func__,
							strerror(errno));
				return -1;
			}
		}
	} while (eagain);

	return sret;
}

int close_socket(int fd) {
#ifdef WIN32
	return closesocket(fd);
#else
	return close(fd);
#endif
}

int recv_buf(int fd, void *data, size_t length)
{
	return recv_buf_timeout(fd, data, length, 0, RECV_TIMEOUT);
}

int peek_buf(int fd, void *data, size_t length)
{
	return recv_buf_timeout(fd, data, length, MSG_PEEK, RECV_TIMEOUT);
}

int recv_buf_timeout(int fd, void *data, size_t length, int flags,
					 unsigned int timeout)
{
	int res;
	int result;

	// check if data is available
	res = check_fd(fd, FDM_READ, timeout);
	if (res <= 0) {
		return res;
	}
	// if we get here, there _is_ data available
	result = recv(fd, data, length, flags);
	if (res > 0 && result == 0) {
		// but this is an error condition
		if (verbose >= 3)
			fprintf(stderr, "%s: fd=%d recv returned 0\n", __func__, fd);
		return -EAGAIN;
	}
	if (result < 0) {
		return -errno;
	}
	return result;
}

int send_buf(int fd, void *data, size_t length)
{
	return send(fd, data, length, 0);
}
