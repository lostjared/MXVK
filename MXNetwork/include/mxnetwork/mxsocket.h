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

/**
 * @brief Cross-platform socket state used by the C API.
 */
typedef struct {
    mx_socket_fd sockfd;    /**< Underlying socket handle. */
    socklen_t addrlen;      /**< Length of the active address structure. */
    bool blocking;          /**< True when the socket is in blocking mode. */
    struct sockaddr_un sun; /**< UNIX-domain address storage. */
    struct sockaddr_in inet; /**< IPv4 address storage. */
} MXSocket;

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Initialize a socket structure with default values.
 * @param s Socket state to initialize.
 * @return True on success.
 */
[[nodiscard]] bool mx_socket_init(MXSocket *s);
/**
 * @brief Create and bind an Internet socket for listening.
 * @param s Socket state to configure.
 * @param port Service or port name to bind.
 * @param backlog Maximum pending connection queue length.
 * @param type Socket kind, typically SOCK_STREAM or SOCK_DGRAM.
 * @return True on success.
 */
[[nodiscard]] bool mx_socket_listen(MXSocket *s, const char *port, int backlog, int type);
/**
 * @brief Create and bind a UNIX-domain socket for listening.
 * @param s Socket state to configure.
 * @param path Filesystem path for the socket.
 * @param backlog Maximum pending connection queue length.
 * @param type Socket kind, typically SOCK_STREAM or SOCK_DGRAM.
 * @return True on success.
 */
[[nodiscard]] bool mx_socket_unix_listen(MXSocket *s, const char *path, int backlog, int type);
/**
 * @brief Accept an incoming connection.
 * @param input Listening socket.
 * @param output Newly accepted socket state.
 * @return True on success.
 */
[[nodiscard]] bool mx_socket_accept(const MXSocket *input, MXSocket *output);
/**
 * @brief Close a socket if it is open.
 * @param s Socket state to close.
 */
void mx_socket_close(MXSocket *s);
/**
 * @brief Toggle blocking mode for a socket.
 * @param s Socket state to update.
 * @param state Desired blocking state.
 * @return True on success.
 */
[[nodiscard]] bool mx_socket_set_blocking(MXSocket *s, bool state);
/**
 * @brief Connect an Internet socket to a remote host.
 * @param s Socket state to configure.
 * @param host Remote host name or address.
 * @param port Remote service or port name.
 * @param type Socket kind, typically SOCK_STREAM or SOCK_DGRAM.
 * @return True on success.
 */
[[nodiscard]] bool mx_socket_connect(MXSocket *s, const char *host, const char *port, int type);
/**
 * @brief Connect a UNIX-domain socket to a local path.
 * @param s Socket state to configure.
 * @param path Filesystem path for the socket.
 * @param type Socket kind, typically SOCK_STREAM or SOCK_DGRAM.
 * @return True on success.
 */
[[nodiscard]] bool mx_socket_unix_connect(MXSocket *s, const char *path, int type);
/**
 * @brief Read data from a socket.
 * @param s Socket state.
 * @param data Output buffer.
 * @param len Number of bytes to read.
 * @param flags Platform socket flags.
 * @return Number of bytes read, or a negative error value.
 */
ssize_t mx_socket_read(MXSocket *s, void *data, size_t len, int flags);
/**
 * @brief Write data to a socket.
 * @param s Socket state.
 * @param data Input buffer.
 * @param len Number of bytes to send.
 * @param flags Platform socket flags.
 * @return Number of bytes written, or a negative error value.
 */
ssize_t mx_socket_send(MXSocket *s, const void *data, size_t len, int flags);
/**
 * @brief Check whether the socket handle is valid.
 * @param s Socket state to inspect.
 * @return True when the handle is valid.
 */
[[nodiscard]] bool mx_socket_valid(const MXSocket *s);
/**
 * @brief Check whether the socket is open.
 * @param s Socket state to inspect.
 * @return True when the socket is open.
 */
[[nodiscard]] bool mx_socket_is_open(const MXSocket *s);
/**
 * @brief Read a line of text from a socket.
 * @param s Socket state.
 * @param buffer Output buffer pointer.
 * @param len Output length.
 * @return True on success.
 */
[[nodiscard]] bool mx_socket_readline(MXSocket *s, char **buffer, size_t *len);
/**
 * @brief Bind an Internet socket to a local port.
 * @param s Socket state to configure.
 * @param port Service or port name.
 * @return True on success.
 */
[[nodiscard]] bool mx_socket_bind(MXSocket *s, const char *port);
/**
 * @brief Bind a UNIX-domain socket to a local path.
 * @param s Socket state to configure.
 * @param path Filesystem path for the socket.
 * @return True on success.
 */
[[nodiscard]] bool mx_socket_unix_bind(MXSocket *s, const char *path);
/**
 * @brief Read the requested number of bytes from a socket.
 * @param sock Socket state.
 * @param buf Output buffer.
 * @param bytes Number of bytes to read.
 * @return Number of bytes read, or a negative error value.
 */
ssize_t mx_socket_read_all(MXSocket *sock, void *buf, size_t bytes);
/**
 * @brief Write the requested number of bytes to a socket.
 * @param sock Socket state.
 * @param buf Input buffer.
 * @param bytes Number of bytes to write.
 * @return Number of bytes written, or a negative error value.
 */
ssize_t mx_socket_write_all(MXSocket *sock, const void *buf, size_t bytes);
/**
 * @brief Send a datagram using an Internet socket.
 * @param sock Socket state.
 * @param buf Input buffer.
 * @param bytes Number of bytes to send.
 * @return Number of bytes sent, or a negative error value.
 */
ssize_t mx_socket_sendto(MXSocket *sock, const void *buf, size_t bytes);
/**
 * @brief Receive a datagram using an Internet socket.
 * @param sock Socket state.
 * @param buf Output buffer.
 * @param bytes Maximum number of bytes to receive.
 * @return Number of bytes received, or a negative error value.
 */
ssize_t mx_socket_recvfrom(MXSocket *sock, void *buf, size_t bytes);
/**
 * @brief Send a datagram using a UNIX-domain socket.
 * @param sock Socket state.
 * @param buf Input buffer.
 * @param bytes Number of bytes to send.
 * @return Number of bytes sent, or a negative error value.
 */
ssize_t mx_socket_unix_sendto(MXSocket *sock, const void *buf, size_t bytes);
/**
 * @brief Receive a datagram using a UNIX-domain socket.
 * @param sock Socket state.
 * @param buf Output buffer.
 * @param bytes Maximum number of bytes to receive.
 * @return Number of bytes received, or a negative error value.
 */
ssize_t mx_socket_unix_recvfrom(MXSocket *sock, void *buf, size_t bytes);
/**
 * @brief Ignore SIGPIPE on platforms that require it.
 */
void mx_socket_ignore_pipe_signal();
#ifdef __cplusplus
}
#endif
#endif
