#include "mxnetwork/socket.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <signal.h>
#include <string>
#include <vector>
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
        std::cerr << "Usage: " << argv[0] << " <port>\n";
        return EXIT_FAILURE;
    }
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
        mxnetwork::Socket sock(mxnetwork::SocketType::TYPE_INET_DGRAM);
        if (sock.bind(argv[1])) {
            std::cout << "Listening for UDP datagram on port " << argv[1] << "...\n";
            std::vector<uint8_t> buffer(65536);
            active_loop.store(true);
            while (active_loop.load()) {
                ssize_t bytes = sock.recvfrom(buffer.data(), buffer.size());
                if (bytes > 0) {
                    std::cout << "Received " << bytes << " bytes.\n";
                    std::string data(buffer.begin(), buffer.end());
                    std::cout << "Data: " << data;
                } else if (bytes < 0) {
                    if (errno == EINTR)
                        continue;
                    std::cerr << "read error.\n";
                    break;
                }
            }
        }
    } catch (const mxnetwork::Exception &e) {
        std::cerr << "Exception: " << e.text() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
