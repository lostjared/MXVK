#include "mxnetwork/socket.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <signal.h>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path>\n";
        return EXIT_FAILURE;
    }
    try {
        mxnetwork::Socket sock(mxnetwork::SocketType::TYPE_UNIX_DGRAM);
        if (sock.connect_unix(argv[1])) {
            std::cout << "Sending. UDP datagram on socket: " << argv[1] << "...\n";
            std::string data = "Hello, World!\n";
            sock.sendto(data.c_str(), data.length());
        }
    } catch (const mxnetwork::Exception &e) {
        std::cerr << "Exception: " << e.text() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
