#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "sock_stuff.h"

#define RECV_TIMEOUT 10000

int create_unix_socket (const char *filename)
{
    struct sockaddr_un name;
    int sock;
    size_t size;

    // remove if still present
    unlink(filename);

    /* Create the socket. */
    sock = socket (PF_LOCAL, SOCK_STREAM, 0);
    if (sock < 0) {
	perror ("socket");
	return -1;
    }

    /* Bind a name to the socket. */
    name.sun_family = AF_LOCAL;
    strncpy (name.sun_path, filename, sizeof (name.sun_path));
    name.sun_path[sizeof (name.sun_path) - 1] = '\0';

    /* The size of the address is
       the offset of the start of the filename,
       plus its length,
       plus one for the terminating null byte.
       Alternatively you can just do:
       size = SUN_LEN (&name);
     */
    size = (offsetof (struct sockaddr_un, sun_path)
	    + strlen (name.sun_path) + 1);

    if (bind (sock, (struct sockaddr *) &name, size) < 0) {
	perror("bind");
	close(sock);
	return -1;
    }

    if (listen(sock, 10) < 0) {
	perror("listen");
	close(sock);
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
	fprintf(stderr, "%s: stat '%s': %s\n", __func__, filename, strerror(errno));
	return -1;
    }

    // ... and if it is a unix domain socket
    if (!S_ISSOCK(fst.st_mode)) {
	fprintf(stderr, "%s: File '%s' is not a socket!\n", __func__, filename);
	return -1;
    }

    // make a new socket
    if ((sfd = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
	fprintf(stderr, "%s: socket: %s\n", __func__, strerror(errno));
	return -1;
    }

    // and connect to 'filename'
    name.sun_family = AF_LOCAL;
    strncpy(name.sun_path, filename, sizeof(name.sun_path));
    name.sun_path[sizeof(name.sun_path) - 1] = 0;

    size = (offsetof (struct sockaddr_un, sun_path)
	    + strlen (name.sun_path) + 1);

    if (connect(sfd, (struct sockaddr*)&name, size) < 0) {
	close(sfd);
	fprintf(stderr, "%s: connect: %s\n", __func__, strerror(errno));
	return -1;
    }

    return sfd;
}

int create_socket(uint16_t port)
{
    int sfd = -1;
    int yes = 1;
    struct sockaddr_in saddr;

    if ( 0 > ( sfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP) ) ) {
	perror("socket()");
	return -1;
    }

    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
	perror("setsockopt()");
	close(sfd);
	return -1;
    }

    memset((void *)&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons(port);

    if(0 > bind(sfd, (struct sockaddr *)&saddr , sizeof(saddr))) {
	perror("bind()");
	close(sfd);
	return -1;
    }
		   
    if (listen(sfd, 1) == -1) {
	perror("listen()");
	close(sfd);
	return -1;
    }

    return sfd;    
}

int connect_socket(const char *addr, uint16_t port)
{
    int sfd = -1;
    int yes = 1;
    struct hostent *hp;
    struct sockaddr_in saddr;

    if (!addr) {
	errno = EINVAL;
	return -1;
    }

    if ((hp = gethostbyname(addr)) == NULL) {
	fprintf(stderr, "%s: unknown host '%s'\n", __func__, addr);
	return -1;
    }

    if (!hp->h_addr) {
	fprintf(stderr, "%s: gethostbyname returned NULL address!\n", __func__);
	return -1;
    }

    if ( 0 > ( sfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP) ) ) {
	perror("socket()");
	return -1;
    }

    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
	perror("setsockopt()");
	close(sfd);
	return -1;
    }

    memset((void *)&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = *(uint32_t*)hp->h_addr;
    saddr.sin_port = htons(port);

    if (connect(sfd, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
	perror("connect");
	close(sfd);
	return -2;
    }

    return sfd;
}

int check_fd(int fd, fd_mode fdm, unsigned int timeout)
{
    fd_set fds;
    int sret;
    int eagain;
    struct timeval to;

    if (fd <= 0) {
	fprintf(stderr, "ERROR: invalid fd in check_fd %d\n", fd);
	return -1;
    }

    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    to.tv_sec = (time_t)(timeout/1000);
    to.tv_usec = (time_t)((timeout-(to.tv_sec*1000))*1000);

    sret = -1;

    do {
	eagain = 0;
	switch(fdm) {
	    case FD_READ:
		sret = select(fd+1,&fds,NULL,NULL,&to);
                break;
            case FD_WRITE:
                sret = select(fd+1,NULL,&fds,NULL,&to);
                break;
            case FD_EXCEPT:
                sret = select(fd+1,NULL,NULL,&fds,&to);
                break;
	}
	
	if (sret < 0) {
	    switch(errno) {
		case EINTR:
		    // interrupt signal in select
		    fprintf(stderr, "%s: EINTR\n", __func__);
		    eagain = 1;
		    break;
		case EAGAIN:
		    fprintf(stderr, "%s: EAGAIN\n", __func__);
		    break;
		default:
		    fprintf(stderr, "%s: select failed: %s\n", __func__, strerror(errno));
		    return -1;
	    }
	}
    } while (eagain);

    return sret;
}

int recv_buf(int fd, void *data, size_t length)
{
    return recv_buf_timeout(fd, data, length, 0, RECV_TIMEOUT);
}

int peek_buf(int fd, void *data, size_t length)
{
    return recv_buf_timeout(fd, data, length, MSG_PEEK, RECV_TIMEOUT);
}

int recv_buf_timeout(int fd, void *data, size_t length, int flags, unsigned int timeout)
{
    int res;
    int result;

    // check if data is available
    res = check_fd(fd, FD_READ, timeout);
    if (res <= 0) {
	return res;
    }

    // if we get here, there _is_ data available
    result = recv(fd, data, length, flags);
    if (res > 0 && result == 0) {
	// but this is an error condition
	fprintf(stderr, "%s: fd=%d recv returned 0\n", __func__, fd);
	return -1;
    }
    return result;
}

int send_buf(int fd, void *data, size_t length)
{
    return send(fd, data, length, 0);
}

