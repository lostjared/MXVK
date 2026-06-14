#include "mxnetwork/socket.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif
#else
#include <signal.h>
#include <sys/socket.h>
#endif

std::atomic<bool> active_loop{false};

#ifdef _WIN32
BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
        active_loop.store(false);
        return TRUE;
    }
    return FALSE;
}
#else
void exit_signal(int) {
    active_loop.store(false);
}
#endif

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "server: Invalid arguments\n"
        << argv[0] << " <port>\n";
        return EXIT_FAILURE;
    }
    mxnetwork::MXNetworkInit net_init;
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(console_handler, TRUE)) {
        std::cerr << "Error setting console handler\n";
        return EXIT_FAILURE;
    }
#else
    struct sigaction sa = {};
    sa.sa_handler = exit_signal;
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }
#endif

    try {
        mx_socket_ignore_pipe_signal();
        mxnetwork::Socket sock(mxnetwork::SocketType::TYPE_INET);
        if (sock.listen(argv[1], 5)) {
            active_loop.store(true);
            while (active_loop.load()) {
                std::optional<mxnetwork::Socket> s = sock.accept();
                if (s) {
                    std::thread t([sockfd = sock.sockfd()](mxnetwork::Socket sfd) {
                        char buffer[256];
                        ssize_t bytes = 0;
                        if ((bytes = sfd.read_all(buffer, 255)) > 0) {
                            buffer[bytes] = '\0';
                    std::string value{buffer};
                    if (value.find("exit") != std::string::npos) {
                        active_loop.store(false);
                        std::cerr << "server: Exiting..\n";
                        shutdown(sockfd, SHUT_RDWR);
                        return;
                    }
                    std::cout << value << "\n";
                        } else {
                            std::cerr << "Error reading stream.\n";
                        }
                    }, std::move(*s));

                    t.detach();
                } else {
                    if (SOCK_ERRNO == SOCK_EINTR)
                        continue;
                    break;
                }
            }
        } else {
            perror("listen");
        }
    } catch (const mxnetwork::Exception &s) {
        std::cerr << "Exception: " << s.text() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
