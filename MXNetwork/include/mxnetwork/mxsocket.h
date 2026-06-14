#ifndef MX_SOCKET_H
#define MX_SOCKET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#if __has_include(<afunix.h>)
#include <afunix.h>
#else
#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif
struct sockaddr_un {
    ADDRESS_FAMILY sun_family;    // AF_UNIX
    char sun_path[UNIX_PATH_MAX]; // pathname
};
#endif
typedef SOCKET mx_socket_fd;
typedef int socklet_t;
typedef ptrdiff_t ssize_t;
#define mx_close_socket(s) closesocket(s)
#define mx_set_err(e) WSASetLastError(WSA##e)
#define SOCK_ERRNO WSAGetLastError()
#define SOCK_EINTR WSAEINTR
#define SOCK_EAGAIN WSAEWOULDBLOCK
#define SOCK_EWOULDBLOCK WSAEWOULDBLOCK
#define SOCK_ECONNABORTED WSAECONNABORTED
#define SOCK_EINVAL WSAEINVAL
#define SOCK_EBADF WSAEBADF
#define SOCK_ENOTSOCK WSAENOTSOCK
#define SOCK_ESHUTDOWN WSAESHUTDOWN
#define SOCK_ECONNRESET WSAECONNRESET
#define NULL_SOCKET INVALID_SOCKET
#define MX_LEN(x) (int)(x)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
typedef int mx_socket_fd;
#define mx_close_socket(s) close(s)
#define mx_set_err(e) (errno = (e))
#define SOCK_ERRNO errno
#define SOCK_EINTR EINTR
#define SOCK_EAGAIN EAGAIN
#define SOCK_EWOULDBLOCK EWOULDBLOCK
#define SOCK_ECONNABORTED ECONNABORTED
#define SOCK_EINVAL EINVAL
#define SOCK_EBADF EBADF
#define SOCK_ENOTSOCK ENOTSOCK
#define SOCK_ESHUTDOWN ESHUTDOWN
#define SOCK_ECONNRESET ECONNRESET
#define MX_LEN(x) (size_t)(x)
#define NULL_SOCKET -1
#endif

typedef struct {
    mx_socket_fd sockfd;
    socklen_t addrlen;
    bool blocking;
    struct sockaddr_un sun;
    struct sockaddr_in inet;
} MXSocket;

#ifdef __cplusplus
extern "C" {
#endif
[[nodiscard]] bool mx_socket_init(MXSocket *s);
[[nodiscard]] bool mx_socket_listen(MXSocket *s, const char *port, int backlog, int type);
[[nodiscard]] bool mx_socket_unix_listen(MXSocket *s, const char *path, int backlog, int type);
[[nodiscard]] bool mx_socket_accept(const MXSocket *input, MXSocket *output);
void mx_socket_close(MXSocket *s);
[[nodiscard]] bool mx_socket_set_blocking(MXSocket *s, bool state);
[[nodiscard]] bool mx_socket_connect(MXSocket *s, const char *host, const char *port, int type);
[[nodiscard]] bool mx_socket_unix_connect(MXSocket *s, const char *path, int type);
ssize_t mx_socket_read(MXSocket *s, void *data, size_t len, int flags);
ssize_t mx_socket_send(MXSocket *s, const void *data, size_t len, int flags);
[[nodiscard]] bool mx_socket_valid(const MXSocket *s);
[[nodiscard]] bool mx_socket_is_open(const MXSocket *s);
[[nodiscard]] bool mx_socket_readline(MXSocket *s, char **buffer, size_t *len);
[[nodiscard]] bool mx_socket_bind(MXSocket *s, const char *port);
[[nodiscard]] bool mx_socket_unix_bind(MXSocket *s, const char *path);
ssize_t mx_socket_read_all(MXSocket *sock, void *buf, size_t bytes);
ssize_t mx_socket_write_all(MXSocket *sock, const void *buf, size_t bytes);
ssize_t mx_socket_sendto(MXSocket *sock, const void *buf, size_t bytes);
ssize_t mx_socket_recvfrom(MXSocket *sock, void *buf, size_t bytes);
ssize_t mx_socket_unix_sendto(MXSocket *sock, const void *buf, size_t bytes);
ssize_t mx_socket_unix_recvfrom(MXSocket *sock, void *buf, size_t bytes);
void mx_socket_ignore_pipe_signal();
#ifdef __cplusplus
}
#endif
#endif
