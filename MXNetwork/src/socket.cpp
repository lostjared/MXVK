#include "mxnetwork/socket.hpp"
#include <exception>
#include <format>
#include <iostream>
#include <string>
namespace mxnetwork {

    MXNetworkInit::MXNetworkInit() {
#ifdef _WIN32
        WSADATA wsaData;
        int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (err != 0) {
            fprintf(stderr, "WSAStartup failed with error: %d\n", err);
            exit(1);
        }
#endif
    }

    MXNetworkInit::~MXNetworkInit() {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    Socket::Socket() noexcept : Socket(SocketType::TYPE_INVALID) {}

    Socket::Socket(SocketType stype) noexcept {
        type = stype;
        if (mx_socket_init(&sock))
            return;
        else
            type = SocketType::TYPE_INVALID;
        sock.sockfd = -1;
    }

    Socket::~Socket() noexcept {
        if (mx_socket_valid(&sock)) {
            close();
        }
    }

    Socket::Socket(int sockfd, SocketType stype) {
        if (!mx_socket_init(&sock))
            throw Exception("Error on socket init.\n");

        sock.sockfd = sockfd;
        sock.addrlen = 0;
        sock.blocking = false;
        type = stype;
    }

    Socket::Socket(const MXSocket &s, SocketType stype) noexcept {
        type = stype;
        setsocket(s);
    }

    Socket::Socket(Socket &&s) noexcept {
        type = s.type;
        setsocket(s.sock);
        s.sock.sockfd = -1;
    }

    Socket &Socket::operator=(Socket &&s) noexcept {
        if (this != &s) {
            type = s.type;
            if (sock.sockfd >= 0)
                ::mx_close_socket(sock.sockfd);
            setsocket(s.sock);
            s.sock.sockfd = -1;
        }
        return *this;
    }

    bool Socket::connect(const std::string_view host, const std::string_view port) {
        if (type == SocketType::TYPE_INET)
            return mx_socket_connect(&sock, std::string(host).c_str(), std::string(port).c_str(), SOCK_STREAM);
        else if (type == SocketType::TYPE_INET_DGRAM)
            return mx_socket_connect(&sock, std::string(host).c_str(), std::string(port).c_str(), SOCK_DGRAM);
        return false;
    }

    bool Socket::connect_unix(const std::string_view path) {
        if (type == SocketType::TYPE_UNIX)
            return mx_socket_unix_connect(&sock, std::string(path).c_str(), SOCK_STREAM);
        else if (type == SocketType::TYPE_UNIX_DGRAM)
            return mx_socket_unix_connect(&sock, std::string(path).c_str(), SOCK_DGRAM);
        return false;
    }

    bool Socket::listen(std::string_view port, int backlog) {
        if (type == SocketType::TYPE_INET)
            return mx_socket_listen(&sock, std::string(port).c_str(), backlog, SOCK_STREAM);
        else if (type == SocketType::TYPE_INET_DGRAM)
            return mx_socket_listen(&sock, std::string(port).c_str(), backlog, SOCK_DGRAM);
        return false;
    }

    bool Socket::listen_unix(std::string_view path, int backlog) {
        if (type == SocketType::TYPE_UNIX)
            return mx_socket_unix_listen(&sock, std::string(path).c_str(), backlog, SOCK_STREAM);
        else if (type == SocketType::TYPE_UNIX_DGRAM)
            return mx_socket_unix_listen(&sock, std::string(path).c_str(), backlog, SOCK_DGRAM);
        return false;
    }

    [[nodiscard]] std::optional<Socket> Socket::accept() {
        MXSocket newsocket;
        if (!mx_socket_init(&newsocket))
            return std::nullopt;

        if (mx_socket_accept(&sock, &newsocket)) {
            return Socket(newsocket, type);
        }
        if (SOCK_ERRNO == SOCK_EINTR || SOCK_ERRNO == SOCK_EAGAIN || SOCK_ERRNO == SOCK_EWOULDBLOCK || SOCK_ERRNO == SOCK_ECONNABORTED || SOCK_ERRNO == SOCK_EINVAL || SOCK_ERRNO == SOCK_EBADF)
            return std::nullopt;

        throw Exception("Error accept socket failed.\n");
    }

    bool Socket::bind(std::string_view port) {
        if (!mx_socket_bind(&sock, std::string(port).c_str())) {
            throw Exception("Could not bind UDP socket.");
        }
        return true;
    }

    bool Socket::bind_unix(std::string_view path) {
        if (!mx_socket_unix_bind(&sock, std::string(path).c_str()))
            return false;
        return true;
    }

    bool Socket::setblocking(bool block) {
        return mx_socket_set_blocking(&sock, block);
    }

    [[nodiscard]] bool Socket::valid() const {
        return mx_socket_valid(&sock);
    }

    [[nodiscard]] bool Socket::is_open() const {
        return mx_socket_is_open(&sock);
    }

    void Socket::close() {
        if (mx_socket_valid(&sock)) {
            std::cout << "Socket: " << sock.sockfd << " closed.\n";
            mx_socket_close(&sock);
        }
        type = SocketType::TYPE_INVALID;
    }

    [[nodiscard]] SocketType Socket::socket_type() const {
        return type;
    }

    [[nodiscard]] int Socket::sockfd() const {
        return sock.sockfd;
    }

    ssize_t Socket::read(void *buf, size_t bytes, int flags) {
        return mx_socket_read(&sock, buf, bytes, flags);
    }

    bool Socket::readline(char **buffer, size_t *len) {
        return mx_socket_readline(&sock, buffer, len);
    }

    ssize_t Socket::write(const void *buf, size_t bytes, int flags) {
        return mx_socket_send(&sock, buf, bytes, flags);
    }

    ssize_t Socket::read_all(void *buf, size_t bytes) {
        return mx_socket_read_all(&sock, buf, bytes);
    }

    ssize_t Socket::write_all(const void *buf, size_t bytes) {
        return mx_socket_write_all(&sock, buf, bytes);
    }

    ssize_t Socket::sendto(const void *buf, size_t bytes) {
        if (type == SocketType::TYPE_INET_DGRAM)
            return mx_socket_sendto(&sock, buf, bytes);
        else if (type == SocketType::TYPE_UNIX_DGRAM)
            return mx_socket_unix_sendto(&sock, buf, bytes);
        return 0;
    }
    ssize_t Socket::recvfrom(void *buf, size_t bytes) {
        if (type == SocketType::TYPE_INET_DGRAM)
            return mx_socket_recvfrom(&sock, buf, bytes);
        else if (type == SocketType::TYPE_UNIX_DGRAM)
            return mx_socket_unix_recvfrom(&sock, buf, bytes);
        return 0;
    }

    void Socket::setsocket(const MXSocket &s) {
        if (!mx_socket_init(&sock))
            return;
        sock.sockfd = s.sockfd;
        sock.blocking = s.blocking;
        sock.addrlen = s.addrlen;
        if (type == SocketType::TYPE_INET || type == SocketType::TYPE_INET_DGRAM) {
            size_t len_size = (static_cast<size_t>(s.addrlen) > sizeof(sock.inet)) ? sizeof(sock.inet) : static_cast<size_t>(s.addrlen);
            memcpy(&sock.inet, &s.inet, len_size);
        } else if (type == SocketType::TYPE_UNIX || type == SocketType::TYPE_UNIX_DGRAM)
            sock.sun = s.sun;
    }
} // namespace mxnetwork
