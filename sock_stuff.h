#ifndef __SOCK_STUFF_H
#define __SOCK_STUFF_H

#include <stdint.h>

enum fd_mode
{
    FD_READ,
    FD_WRITE,
    FD_EXCEPT
};
typedef enum fd_mode fd_mode;

int create_unix_socket(const char *filename);
int connect_unix_socket(const char *filename);
int create_socket(uint16_t port);
int connect_socket(const char *addr, uint16_t port);
int check_fd(int fd, fd_mode fdm, unsigned int timeout);

int recv_buf(int fd, void *data, size_t size);
int peek_buf(int fd, void *data, size_t size);
int recv_buf_timeout(int fd, void *data, size_t size, int flags, unsigned int timeout);

int send_buf(int fd, void *data, size_t size);

void sock_stuff_set_verbose(int level);

#endif /* __SOCK_STUFF_H */

