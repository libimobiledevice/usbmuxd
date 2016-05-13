#define nfds_t ULONG

#define POLLRDNORM 0x0100
#define POLLRDBAND 0x0200
#define POLLIN    (POLLRDNORM | POLLRDBAND)

#define POLLWRNORM 0x0010
#define POLLOUT (POLLWRNORM)

/* Mapping of BSD names to Windows names */
#define EWOULDBLOCK WSAEWOULDBLOCK
#define sockaddr_un sockaddr_in