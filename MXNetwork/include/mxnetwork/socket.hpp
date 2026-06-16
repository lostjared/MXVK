#ifndef MXSOCKET_H
#define MXSOCKET_H

#include "mxnetwork/exception.hpp"
#include "mxnetwork/mxsocket.h"
#include <optional>
#include <string>
#include <string_view>

namespace mxnetwork {

    /**
     * @brief RAII helper that initializes and shuts down the platform socket subsystem.
     */
    struct MXNetworkInit {
        /**
         * @brief Initialize the socket backend for the current process.
         */
        MXNetworkInit();
        /**
         * @brief Shut down the socket backend.
         */
        ~MXNetworkInit();
        MXNetworkInit(const MXNetworkInit &) = delete;
        MXNetworkInit &operator=(const MXNetworkInit &) = delete;
    };

    /**
     * @brief Socket family and transport type.
     */
    enum class SocketType {
        /** @brief No socket type selected. */
        TYPE_INVALID = 0,
        /** @brief IPv4 stream socket. */
        TYPE_INET,
        /** @brief UNIX-domain stream socket. */
        TYPE_UNIX,
        /** @brief IPv4 datagram socket. */
        TYPE_INET_DGRAM,
        /** @brief UNIX-domain datagram socket. */
        TYPE_UNIX_DGRAM
    };

    /**
     * @brief C++ wrapper around the MXNetwork socket API.
     */
    class Socket {
      public:
        /** @brief Construct an invalid socket wrapper. */
        Socket() noexcept;
        /**
         * @brief Construct a socket wrapper for the given type.
         * @param type Socket family and transport type.
         */
        Socket(SocketType type) noexcept;
        /** @brief Destroy the socket wrapper and close the socket if needed. */
        ~Socket() noexcept;
        /**
         * @brief Adopt an existing socket handle.
         * @param sockfd Socket handle to wrap.
         * @param type Socket family and transport type.
         */
        Socket(mx_socket_fd sockfd, SocketType type);
        /**
         * @brief Copy socket state from a C API structure.
         * @param s Source socket state.
         * @param type Socket family and transport type.
         */
        Socket(const MXSocket &s, SocketType type) noexcept;
        Socket(const Socket &s) = delete;
        /** @brief Move-construct from another socket wrapper. */
        Socket(Socket &&s) noexcept;
        Socket &operator=(const Socket &s) = delete;
        /** @brief Move-assign from another socket wrapper. */
        Socket &operator=(Socket &&s) noexcept;
        /** @brief Return the underlying socket handle. */
        [[nodiscard]] mx_socket_fd sockfd() const;
        /**
         * @brief Connect to a remote Internet endpoint.
         * @param host Remote host name or address.
         * @param port Remote service or port name.
         * @return True on success.
         */
        bool connect(const std::string_view host, const std::string_view port);
        /**
         * @brief Connect to a UNIX-domain endpoint.
         * @param path Filesystem path for the socket.
         * @return True on success.
         */
        bool connect_unix(const std::string_view path);
        /**
         * @brief Start listening on an Internet port.
         * @param port Service or port name.
         * @param backlog Maximum pending connection queue length.
         * @return True on success.
         */
        bool listen(std::string_view port, int backlog);
        /**
         * @brief Start listening on a UNIX-domain socket path.
         * @param path Filesystem path for the socket.
         * @param backlog Maximum pending connection queue length.
         * @return True on success.
         */
        bool listen_unix(std::string_view path, int backlog);
        /**
         * @brief Toggle blocking mode.
         * @param block True to enable blocking I/O.
         * @return True on success.
         */
        bool setblocking(bool block);
        /**
         * @brief Bind an Internet socket to a local port.
         * @param port Service or port name.
         * @return True on success.
         */
        bool bind(std::string_view port);
        /**
         * @brief Bind a UNIX-domain socket to a local path.
         * @param path Filesystem path for the socket.
         * @return True on success.
         */
        bool bind_unix(std::string_view path);
        /**
         * @brief Accept an incoming connection.
         * @return Accepted socket on success.
         */
        [[nodiscard]] std::optional<Socket> accept();

        /**
         * @brief Read bytes from the socket.
         * @param buf Output buffer.
         * @param bytes Number of bytes to read.
         * @param flags Platform socket flags.
         * @return Number of bytes read, or a negative error value.
         */
        ssize_t read(void *buf, size_t bytes, int flags);
        /**
         * @brief Read a line of text from the socket.
         * @param buffer Output buffer pointer.
         * @param len Output length.
         * @return True on success.
         */
        bool readline(char **buffer, size_t *len);
        /**
         * @brief Write bytes to the socket.
         * @param buf Input buffer.
         * @param bytes Number of bytes to write.
         * @param flags Platform socket flags.
         * @return Number of bytes written, or a negative error value.
         */
        ssize_t write(const void *buf, size_t bytes, int flags);
        /**
         * @brief Read exactly the requested number of bytes.
         * @param buf Output buffer.
         * @param bytes Number of bytes to read.
         * @return Number of bytes read, or a negative error value.
         */
        ssize_t read_all(void *buf, size_t bytes);
        /**
         * @brief Write exactly the requested number of bytes.
         * @param buf Input buffer.
         * @param bytes Number of bytes to write.
         * @return Number of bytes written, or a negative error value.
         */
        ssize_t write_all(const void *buf, size_t bytes);
        /**
         * @brief Send a datagram using the configured socket type.
         * @param buf Input buffer.
         * @param bytes Number of bytes to send.
         * @return Number of bytes sent, or a negative error value.
         */
        ssize_t sendto(const void *buf, size_t bytes);
        /**
         * @brief Receive a datagram using the configured socket type.
         * @param buf Output buffer.
         * @param bytes Maximum number of bytes to receive.
         * @return Number of bytes received, or a negative error value.
         */
        ssize_t recvfrom(void *buf, size_t bytes);

        /** @brief Return true when the socket handle is valid. */
        [[nodiscard]] bool valid() const;
        /** @brief Return true when the socket is open. */
        [[nodiscard]] bool is_open() const;
        /** @brief Close the socket if it is open. */
        void close();
        /** @brief Return the socket family and transport type. */
        [[nodiscard]] SocketType socket_type() const;

      protected:
        MXSocket sock;
        SocketType type = SocketType::TYPE_INVALID;

      private:
        void setsocket(const MXSocket &s);
    };
} // namespace mxnetwork

#endif
