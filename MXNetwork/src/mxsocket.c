#include "mxnetwork/mxsocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <afunix.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <unistd.h>

#endif

static void mx_socket_store_inet_address(MXSocket *sock, const struct sockaddr *addr, socklen_t addrlen) {
    if (sock == nullptr || addr == nullptr)
        return;

    sock->addrlen = addrlen;
    if (addr->sa_family == AF_INET6) {
        size_t len_size = ((size_t)addrlen > sizeof(sock->inet6)) ? sizeof(sock->inet6) : (size_t)addrlen;
        memcpy(&sock->inet6, addr, len_size);
    } else if (addr->sa_family == AF_INET) {
        size_t len_size = ((size_t)addrlen > sizeof(sock->inet)) ? sizeof(sock->inet) : (size_t)addrlen;
        memcpy(&sock->inet, addr, len_size);
    }
}

static bool mx_socket_inet_listen(MXSocket *sock, const char *port, int backlog, int type, int family) {
    if (port == nullptr)
        return false;

    if (!mx_socket_init(sock))
        return false;

    struct addrinfo hints;
    struct addrinfo *rt, *rp;
    mx_socket_fd sfd = NULL_SOCKET, s;
    int optval = 1;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_canonname = nullptr;
    hints.ai_addr = nullptr;
    hints.ai_next = nullptr;
    hints.ai_socktype = type;
    hints.ai_family = family;
    hints.ai_flags = AI_PASSIVE;
    s = getaddrinfo(nullptr, port, &hints, &rt);
    if (s != 0)
        return false;

    for (rp = rt; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
#ifdef _WIN32
        if (sfd == INVALID_SOCKET)
#else
        if (sfd == -1)
#endif
            continue;

#ifdef _WIN32
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval)) == -1) {
#else
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
#endif
            mx_close_socket(sfd);
            freeaddrinfo(rt);
            return false;
        }

        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        if (sfd != NULL_SOCKET)
            mx_close_socket(sfd);
        sfd = NULL_SOCKET;
    }

    if (sfd == NULL_SOCKET) {
        freeaddrinfo(rt);
        return false;
    }

    if (rp != nullptr && sfd != NULL_SOCKET) {
        if (listen(sfd, backlog) == -1) {
            freeaddrinfo(rt);
            mx_close_socket(sfd);
            return false;
        }
        sock->sockfd = sfd;
        mx_socket_store_inet_address(sock, rp->ai_addr, rp->ai_addrlen);
    } else {
        if (sfd != NULL_SOCKET)
            mx_close_socket(sfd);
        freeaddrinfo(rt);
        return false;
    }

    freeaddrinfo(rt);
    return true;
}

static bool mx_socket_inet_connect(MXSocket *sock, const char *host, const char *port, int type, int family) {
    if (host == nullptr || port == nullptr)
        return false;

    if (!mx_socket_init(sock))
        return false;

    struct addrinfo hints;
    struct addrinfo *rt, *rp;
    mx_socket_fd sfd = NULL_SOCKET, s;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_canonname = nullptr;
    hints.ai_addr = nullptr;
    hints.ai_next = nullptr;
    hints.ai_family = family;
    hints.ai_socktype = type;
    s = getaddrinfo(host, port, &hints, &rt);
    if (s != 0) {
        errno = ENOSYS;
        return false;
    }
    for (rp = rt; rp != nullptr; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
#ifdef _WIN32
        if (sfd == INVALID_SOCKET)
#else
        if (sfd == -1)
#endif
            continue;

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;

        if (sfd != NULL_SOCKET) {
            mx_close_socket(sfd);
            sfd = NULL_SOCKET;
        }
    }

    if (rp != nullptr) {
        sock->sockfd = sfd;
        mx_socket_store_inet_address(sock, rp->ai_addr, rp->ai_addrlen);
    } else {
        freeaddrinfo(rt);
        if (sfd != NULL_SOCKET)
            mx_close_socket(sfd);
        return false;
    }

    freeaddrinfo(rt);
    return true;
}

static bool mx_socket_inet_bind(MXSocket *sock, const char *port, int family) {
    if (!mx_socket_init(sock) || port == nullptr)
        return false;

    mx_socket_fd sockfd = NULL_SOCKET;
    struct addrinfo hints = {};
    struct addrinfo *result = nullptr;
    struct addrinfo *rp = nullptr;
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    int s = getaddrinfo(nullptr, port, &hints, &result);
    if (s != 0) {
        return false;
    }
    bool set_value = false;
    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
#ifdef _WIN32
        if (sockfd == INVALID_SOCKET)
#else
        if (sockfd == -1)
#endif
            continue;

        if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            mx_socket_store_inet_address(sock, rp->ai_addr, rp->ai_addrlen);
            set_value = true;
            break;
        }
        mx_close_socket(sockfd);
        sockfd = NULL_SOCKET;
    }
    freeaddrinfo(result);
    if (!set_value)
        return false;
    sock->sockfd = sockfd;
    return true;
}

[[nodiscard]] bool mx_socket_unix_listen(MXSocket *sock, const char *path, int backlog, int type) {
    if (path == nullptr)
        return false;
    if (!mx_socket_init(sock))
        return false;
    struct sockaddr_un addr;
    mx_socket_fd sockfd = socket(AF_UNIX, type, 0);
#ifdef _WIN32
    if (sockfd == INVALID_SOCKET) {
#else
    if (sockfd == -1) {
#endif
        perror("socket");
        return false;
    }
    if (remove(path) == -1 && errno == ENOENT) {
        perror("remove");
        mx_close_socket(sockfd);
        return false;
    }
    memset(&addr, 0, sizeof(struct sockaddr_un));
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    addr.sun_family = AF_UNIX;
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
        perror("bind");
        mx_close_socket(sockfd);
        return false;
    }

    if (listen(sockfd, backlog) == -1) {
        perror("listen");
        mx_close_socket(sockfd);
        return false;
    }
    sock->sockfd = sockfd;
    sock->sun = addr;
    return true;
}

[[nodiscard]] bool mx_socket_unix_connect(MXSocket *sock, const char *path, int type) {
    if (path == nullptr)
        return false;
    if (!mx_socket_init(sock))
        return false;
    mx_socket_fd sockfd = NULL_SOCKET;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    sockfd = socket(AF_UNIX, type, 0);
#ifdef _WIN32
    if (sockfd == INVALID_SOCKET) {
#else
    if (sockfd == -1) {
#endif
        perror("socket");
        return false;
    }
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
        perror("connect");
        mx_close_socket(sockfd);
        return false;
    }
    sock->sockfd = sockfd;
    sock->sun = addr;
    return true;
}

[[nodiscard]] bool mx_socket_listen(MXSocket *sock, const char *port, int backlog, int type) {
    return mx_socket_inet_listen(sock, port, backlog, type, AF_INET);
}

[[nodiscard]] bool mx_socket_ipv6_listen(MXSocket *sock, const char *port, int backlog, int type) {
    return mx_socket_inet_listen(sock, port, backlog, type, AF_INET6);
}

[[nodiscard]] bool mx_socket_accept(const MXSocket *input, MXSocket *output) {
    if (input == nullptr || output == nullptr)
        return false;

    if (!mx_socket_valid(input))
        return false;

    mx_socket_fd newfd = accept(input->sockfd, 0, 0);
#ifdef _WIN32
    if (newfd == INVALID_SOCKET)
        return false;
    u_long mode = input->blocking ? 0 : 1;
    if (ioctlsocket(newfd, FIONBIO, &mode) != 0) {
        mx_close_socket(newfd);
        return false;
    }
#else
    if (newfd == NULL_SOCKET)
        return false;

    int flags = fcntl(newfd, F_GETFL);
    if (flags == -1) {
        close(newfd);
        return false;
    }
    if (input->blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;
    if (fcntl(newfd, F_SETFL, flags) == -1) {
        mx_close_socket(newfd);
        return false;
    }
#endif
    if (mx_socket_valid(output))
        mx_socket_close(output);
    output->sockfd = newfd;
    output->addrlen = input->addrlen;
    output->blocking = input->blocking;
    output->sun = input->sun;
    output->inet = input->inet;
    output->inet6 = input->inet6;
    return true;
}

void mx_socket_close(MXSocket *sock) {
    if (sock == nullptr)
        return;
    if (mx_socket_valid(sock))
        mx_close_socket(sock->sockfd);
    sock->sockfd = NULL_SOCKET;
}

[[nodiscard]] bool mx_socket_set_blocking(MXSocket *sock, bool state) {
    if (sock == nullptr)
        return false;
    if (mx_socket_valid(sock)) {
#ifdef _WIN32
        u_long mode = state ? 0 : 1;
        if (ioctlsocket(sock->sockfd, FIONBIO, &mode) != 0) {
            fprintf(stderr, "Error setting flags for: %d\n", (int)sock->sockfd);
            return false;
        }
#else
        int flags = fcntl(sock->sockfd, F_GETFL);
        if (flags == -1) {
            fprintf(stderr, "Error getting flags for: %d\n", sock->sockfd);
            return false;
        }
        if (state)
            flags &= ~O_NONBLOCK;
        else
            flags |= O_NONBLOCK;
        if (fcntl(sock->sockfd, F_SETFL, flags) == -1) {
            fprintf(stderr, "Error setting flags for: %d\n", sock->sockfd);
            return false;
        }
#endif
        sock->blocking = state;
    } else
        return false;
    return true;
}

[[nodiscard]] bool mx_socket_connect(MXSocket *sock, const char *host, const char *port, int type) {
    return mx_socket_inet_connect(sock, host, port, type, AF_INET);
}

[[nodiscard]] bool mx_socket_ipv6_connect(MXSocket *sock, const char *host, const char *port, int type) {
    return mx_socket_inet_connect(sock, host, port, type, AF_INET6);
}

[[nodiscard]] bool mx_socket_bind(MXSocket *sock, const char *port) {
    return mx_socket_inet_bind(sock, port, AF_INET);
}

[[nodiscard]] bool mx_socket_ipv6_bind(MXSocket *sock, const char *port) {
    return mx_socket_inet_bind(sock, port, AF_INET6);
}

[[nodiscard]] bool mx_socket_unix_bind(MXSocket *s, const char *path) {
    if (!mx_socket_init(s) || path == nullptr)
        return false;

    mx_socket_fd sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
#ifdef _WIN32
    if (sockfd == INVALID_SOCKET) {
#else
    if (sockfd == -1) {
#endif
        perror("socket");
        return false;
    }
    remove(path);
    struct sockaddr_un caddy;
    memset(&caddy, 0, sizeof(struct sockaddr_un));
    caddy.sun_family = AF_UNIX;
    strncpy(caddy.sun_path, path, sizeof(caddy.sun_path) - 1);
    if (bind(sockfd, (struct sockaddr *)&caddy, sizeof(caddy)) == -1) {
        perror("bind");
        mx_close_socket(sockfd);
        return false;
    }
    s->sun = caddy;
    s->addrlen = sizeof(caddy);
    s->sockfd = sockfd;
    return true;
}

[[nodiscard]] bool mx_socket_init(MXSocket *sock) {
    if (sock == nullptr)
        return false;
    memset(sock, 0, sizeof(MXSocket));
    sock->sockfd = NULL_SOCKET;
    sock->blocking = true;
    return true;
}

[[nodiscard]] bool mx_socket_valid(const MXSocket *sock) {
    if (sock == nullptr)
        return false;
    return sock->sockfd != NULL_SOCKET;
}

ssize_t mx_socket_read(MXSocket *sock, void *buf, size_t len, int flags) {
    if (sock == nullptr || buf == nullptr || len == 0)
        return -1;
    if (!mx_socket_valid(sock)) {
        mx_set_err(EBADF);
        return -1;
    }
    return recv(sock->sockfd, (char *)buf, MX_LEN(len), flags);
}

ssize_t mx_socket_send(MXSocket *sock, const void *buf, size_t len, int flags) {
    if (sock == nullptr || buf == nullptr || len == 0)
        return -1;
    if (!mx_socket_valid(sock)) {
        mx_set_err(EBADF);
        return -1;
    }
    return send(sock->sockfd, (const char *)buf, MX_LEN(len), flags);
}

[[nodiscard]] bool mx_socket_is_open(const MXSocket *sock) {
    if (sock == nullptr || !mx_socket_valid(sock))
        return false;
    char c = 0;
#ifdef _WIN32
    ssize_t r = recv(sock->sockfd, &c, 1, MSG_PEEK);
#elif defined(MX_HAVE_MSG_DONTWAIT)
    ssize_t r = recv(sock->sockfd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
#else
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock->sockfd, &readfds);

    struct timeval timeout = {0, 0};
    const int ready = select((int)(sock->sockfd + 1), &readfds, nullptr, nullptr, &timeout);
    if (ready < 0)
        return false;
    if (ready == 0)
        return true;

    ssize_t r = recv(sock->sockfd, &c, 1, MSG_PEEK);
#endif
    if (r == 0)
        return false;
    if (r > 0)
        return true;
    if (r < 0 && (SOCK_ERRNO == SOCK_EAGAIN || SOCK_ERRNO == SOCK_EWOULDBLOCK)) {
        return true;
    }
    return false;
}

[[nodiscard]] bool mx_socket_readline(MXSocket *sock, char **buffer, size_t *size) {
    if (sock == nullptr || buffer == nullptr || size == nullptr)
        return false;

    if (!mx_socket_valid(sock)) {
        mx_set_err(EBADF);
        return false;
    }

    *buffer = nullptr;
    *size = 0;

    size_t init_size = 4096;
    char *temp = (char *)malloc(init_size + 1);
    if (temp == nullptr)
        return false;
    char c = 0;
    size_t index = 0;
    while (1) {
        ssize_t read_val = recv(sock->sockfd, &c, 1, 0);
        if (read_val > 0) {
            if (c == '\n')
                break;
            if (index >= init_size) {
                size_t new_init_size = init_size * 2;
                char *t = (char *)realloc(temp, new_init_size + 1);
                if (t == nullptr) {
                    free(temp);
                    return false;
                }
                temp = t;
                init_size = new_init_size;
            }
            temp[index++] = c;
            continue;
        }
        if (read_val == 0) {
            if (index == 0) {
                free(temp);
                return false;
            }
            break;
        }

        if (SOCK_ERRNO == SOCK_EINTR)
            continue;

        if (SOCK_ERRNO == SOCK_EAGAIN || SOCK_ERRNO == SOCK_EWOULDBLOCK) {
            if (!sock->blocking)
                break;
            continue;
        }
        free(temp);
        return false;
    }
    temp[index] = 0;
    *buffer = temp;
    *size = index;
    return true;
}

ssize_t mx_socket_write_all(MXSocket *sock, const void *buf, size_t bytes) {
    if (sock == nullptr || buf == nullptr || bytes == 0)
        return -1;

    const char *ptr = (const char *)buf;
    size_t left = bytes;
    while (left > 0) {
        ssize_t written = send(sock->sockfd, (const char *)ptr, MX_LEN(left), 0);
        if (written == -1) {
            if (SOCK_ERRNO == SOCK_EINTR) {
                continue;
            }
            return -1;
        }
        left -= (size_t)written;
        ptr += written;
    }
    return (ssize_t)bytes;
}

ssize_t mx_socket_read_all(MXSocket *sock, void *buf, size_t bytes) {
    if (sock == nullptr || buf == nullptr || bytes == 0)
        return -1;

    char *ptr = (char *)buf;
    size_t left = bytes;
    while (left > 0) {
        ssize_t bytes_read = recv(sock->sockfd, (char *)ptr, MX_LEN(left), 0);
        if (bytes_read == -1) {
            if (SOCK_ERRNO == SOCK_EINTR) {
                continue;
            }
            return -1;
        }
        if (bytes_read == 0)
            break;
        left -= (size_t)bytes_read;
        ptr += bytes_read;
    }
    return (ssize_t)(bytes - left);
}

void mx_socket_ignore_pipe_signal() {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
}

ssize_t mx_socket_sendto(MXSocket *sock, const void *buf, size_t src_bytes) {
    if (sock == nullptr || buf == nullptr || src_bytes == 0)
        return -1;

    ssize_t bytes = 0;
    bytes = sendto(sock->sockfd, (const char *)buf, MX_LEN(src_bytes), 0, (struct sockaddr *)&sock->inet, sock->addrlen);
    return bytes;
}

ssize_t mx_socket_ipv6_sendto(MXSocket *sock, const void *buf, size_t src_bytes) {
    if (sock == nullptr || buf == nullptr || src_bytes == 0)
        return -1;

    ssize_t bytes = 0;
    bytes = sendto(sock->sockfd, (const char *)buf, MX_LEN(src_bytes), 0, (struct sockaddr *)&sock->inet6, sock->addrlen);
    return bytes;
}

ssize_t mx_socket_recvfrom(MXSocket *sock, void *buf, size_t src_bytes) {
    if (sock == nullptr || buf == nullptr || src_bytes == 0)
        return -1;

    ssize_t bytes = 0;
    socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
    struct sockaddr_storage caddr;
    bytes = recvfrom(sock->sockfd, (char *)buf, MX_LEN(src_bytes), 0, (struct sockaddr *)&caddr, &len);
    return bytes;
}

ssize_t mx_socket_ipv6_recvfrom(MXSocket *sock, void *buf, size_t src_bytes) {
    if (sock == nullptr || buf == nullptr || src_bytes == 0)
        return -1;

    ssize_t bytes = 0;
    socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
    struct sockaddr_storage caddr;
    bytes = recvfrom(sock->sockfd, (char *)buf, MX_LEN(src_bytes), 0, (struct sockaddr *)&caddr, &len);
    return bytes;
}

ssize_t mx_socket_unix_sendto(MXSocket *sock, const void *buf, size_t src_bytes) {
    if (sock == nullptr || buf == nullptr || src_bytes == 0)
        return -1;

    ssize_t bytes = 0;
    bytes = sendto(sock->sockfd, (const char *)buf, MX_LEN(src_bytes), 0, (struct sockaddr *)&sock->sun, sock->addrlen);
    return bytes;
}
ssize_t mx_socket_unix_recvfrom(MXSocket *sock, void *buf, size_t src_bytes) {
    if (sock == nullptr || buf == nullptr || src_bytes == 0)
        return -1;

    ssize_t bytes = 0;
    socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
    struct sockaddr_storage caddr;
    bytes = recvfrom(sock->sockfd, (char *)buf, MX_LEN(src_bytes), 0, (struct sockaddr *)&caddr, &len);
    return bytes;
}
