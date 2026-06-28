# MXNetwork

MXNetwork is a small C++20 socket library with a C-compatible low-level API and a move-only C++ RAII wrapper. It builds a static library named `mxnetwork` and provides examples for TCP, UDP, Unix-domain sockets, a simple relay server, an HTTP file downloader, and an optional Qt relay client.

The C API centers on `MXSocket` in `mxnetwork/mxsocket.hpp`. The C++ API wraps it with `mxnetwork::Socket` in `mxnetwork/socket.hpp`.

## Features

- IPv4 TCP client and server sockets.
- IPv4 UDP datagram sockets.
- Unix-domain stream and datagram sockets where supported.
- Windows support through WinSock2.
- Blocking and non-blocking socket mode support.
- Move-only C++ socket ownership with automatic close in the destructor.
- Helpers for `read`, `write`, `read_all`, `write_all`, `sendto`, `recvfrom`, and line reads.
- CMake install/export support for `find_package(mxnetwork)`.

## Requirements

- CMake 3.10 or newer.
- A C++20 compiler.
- pthreads or the platform thread library discovered through CMake `Threads`.
- On Unix-like systems: socket headers such as `sys/socket.h`, `sys/un.h`, `arpa/inet.h`, `netdb.h`, `fcntl.h`, `poll.h`, and `unistd.h`.
- On Windows: `windows.h`, `winsock2.h`, and `ws2tcpip.h`. `afunix.h` is used when available, with a local fallback definition for `sockaddr_un`.
- Qt6 Core, Widgets, and Network only when building the optional relay client.

## Build

```sh
cmake -S . -B build
cmake --build build
```

By default, CMake builds the static library and examples:

- `mxnetwork-client`
- `mxnetwork-server`
- `mxnetwork-recv-udp`
- `mxnetwork-send-udp`
- `mxnetwork-unix-recv-udp`
- `mxnetwork-unix-send-udp`

On Unix-like systems, the default examples also include:

- `download-file`
- `mxnetwork-relay`

Disable examples with `EXAMPLES=OFF`:

```sh
cmake -S . -B build -DEXAMPLES=OFF
cmake --build build
```

Debug builds enable `DEBUG_MODE`, debug symbols, strict GCC warnings, and address sanitizer when the Qt client is not enabled:

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

When no debug build type is selected, the top-level CMake config sets release mode and adds optimization flags.

## Optional Qt Relay Client

The Qt relay client is disabled by default. Enable it with `CLIENT=ON`:

```sh
cmake -S . -B build-client -DCLIENT=ON
cmake --build build-client
```

This adds the `relay_client` executable from `examples/relay-client`. It uses Qt's `QTcpSocket` directly for the GUI client.

## Install

```sh
cmake --install build
```

The install exports a CMake package. A consuming project can link the installed library like this:

```cmake
find_package(mxnetwork REQUIRED)

add_executable(my-app main.cpp)
target_link_libraries(my-app PRIVATE mxnetwork::mxnetwork)
```

When using this repository directly as a subdirectory, link against the local alias:

```cmake
add_subdirectory(path/to/MXNetwork)

add_executable(my-app main.cpp)
target_link_libraries(my-app PRIVATE libmxnetwork::mxnetwork)
```

## Basic Usage

```cpp
#include "mxnetwork/socket.hpp"

#include <iostream>
#include <string>

int main() {
    mxnetwork::MXNetworkInit network_init;
    mx_socket_ignore_pipe_signal();

    mxnetwork::Socket sock(mxnetwork::SocketType::TYPE_INET);
    if (!sock.connect("127.0.0.1", "8080")) {
        std::cerr << "connect failed\n";
        return 1;
    }

    std::string message = "hello";
    if (sock.write_all(message.data(), message.size()) < 0) {
        std::cerr << "write failed\n";
        return 1;
    }

    return 0;
}
```

`mxnetwork::MXNetworkInit` initializes and cleans up WinSock on Windows. It is harmless to create on other platforms.

The main socket types are:

- `mxnetwork::SocketType::TYPE_INET` for IPv4 TCP sockets.
- `mxnetwork::SocketType::TYPE_INET6` for IPv6 TCP sockets.
- `mxnetwork::SocketType::TYPE_UNIX` for Unix-domain stream sockets.
- `mxnetwork::SocketType::TYPE_INET_DGRAM` for IPv4 UDP sockets.
- `mxnetwork::SocketType::TYPE_INET6_DGRAM` for IPv6 UDP sockets.
- `mxnetwork::SocketType::TYPE_UNIX_DGRAM` for Unix-domain datagram sockets.

## Examples

After building, run examples from the build directory.

TCP server and client:

```sh
./build/examples/server/mxnetwork-server 9000
./build/examples/client/mxnetwork-client 127.0.0.1 9000
```

UDP receiver and sender:

```sh
./build/examples/udp-recv/mxnetwork-recv-udp 9001
./build/examples/udp-send/mxnetwork-send-udp 127.0.0.1 9001
```

Unix datagram receiver and sender:

```sh
./build/examples/unix-udp-recv/mxnetwork-unix-recv-udp /tmp/mxnetwork.sock
./build/examples/unix-udp-send/mxnetwork-unix-send-udp /tmp/mxnetwork.sock
```

Relay server, available on Unix-like builds:

```sh
./build/examples/relay/mxnetwork-relay 9002
```

HTTP file download example, available on Unix-like builds:

```sh
./build/examples/download/download-file example.com 80 /index.html index.html
```

Qt relay client, when built with `CLIENT=ON`:

```sh
./build/examples/relay-client/relay_client 127.0.0.1 9002 username
```

## API Overview

Include `mxnetwork/socket.hpp` to use the C++ wrapper. The wrapper owns the socket descriptor, closes it on destruction, cannot be copied, and can be moved.

Common `mxnetwork::Socket` methods:

- `connect(host, port)` and `connect_unix(path)`.
- `listen(port, backlog)` and `listen_unix(path, backlog)`.
- `accept()` returning `std::optional<mxnetwork::Socket>`.
- `bind(port)` and `bind_unix(path)` for datagram receivers.
- `setblocking(bool)`.
- `read`, `write`, `read_all`, `write_all`, and `readline`.
- `sendto` and `recvfrom` for datagram sockets.
- `valid()`, `is_open()`, `sockfd()`, `socket_type()`, and `close()`.

Include `mxnetwork/mxsocket.hpp` to use the C-style API directly.

## License

MXNetwork is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for the full license text.
