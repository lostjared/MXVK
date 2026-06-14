#ifndef MXSOCKET_H
#define MXSOCKET_H

#include "mxnetwork/exception.hpp"
#include "mxnetwork/mxsocket.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace mxnetwork {

    struct MXNetworkInit {
        MXNetworkInit();
        ~MXNetworkInit();
        MXNetworkInit(const MXNetworkInit &) = delete;
        MXNetworkInit &operator=(const MXNetworkInit &) = delete;
    };

    enum class SocketType {
        TYPE_INVALID = 0,
        TYPE_INET,
        TYPE_UNIX,
        TYPE_INET_DGRAM,
        TYPE_UNIX_DGRAM
    };

    class Socket {
      public:
        Socket() noexcept;
        Socket(SocketType type) noexcept;
        ~Socket() noexcept;
        Socket(int sockfd, SocketType type);
        Socket(const MXSocket &s, SocketType type) noexcept;
        Socket(const Socket &s) = delete;
        Socket(Socket &&s) noexcept;
        Socket &operator=(const Socket &s) = delete;
        Socket &operator=(Socket &&s) noexcept;
        [[nodiscard]] int sockfd() const;
        bool connect(const std::string_view host, const std::string_view port);
        bool connect_unix(const std::string_view path);
        bool listen(std::string_view port, int backlog);
        bool listen_unix(std::string_view path, int backlog);
        bool setblocking(bool block);
        bool bind(std::string_view port);
        bool bind_unix(std::string_view path);
        [[nodiscard]] std::optional<Socket> accept();

        ssize_t read(void *buf, size_t bytes, int flags);
        bool readline(char **buffer, size_t *len);
        ssize_t write(const void *buf, size_t bytes, int flags);
        ssize_t read_all(void *buf, size_t bytes);
        ssize_t write_all(const void *buf, size_t bytes);
        ssize_t sendto(const void *buf, size_t bytes);
        ssize_t recvfrom(void *buf, size_t bytes);

        [[nodiscard]] bool valid() const;
        [[nodiscard]] bool is_open() const;
        void close();
        [[nodiscard]] SocketType socket_type() const;

      protected:
        MXSocket sock;
        SocketType type = SocketType::TYPE_INVALID;

      private:
        void setsocket(const MXSocket &s);
    };
} // namespace mxnetwork

#endif
