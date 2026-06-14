#include "mxnetwork/socket.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <signal.h>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <host> <port>\n";
        return EXIT_FAILURE;
    }
    try {
        mxnetwork::Socket sock(mxnetwork::SocketType::TYPE_INET_DGRAM);
        if (sock.connect(argv[1], argv[2])) {
            std::cout << "Sending. UDP datagram on port " << argv[1] << "...\n";
            std::string data = "Hello, World!\n";
            sock.sendto(data.c_str(), data.length());
        }
    } catch (const mxnetwork::Exception &e) {
        std::cerr << "Exception: " << e.text() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
