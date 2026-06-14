#include "mxnetwork/socket.hpp"
#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "client: Invalid arguments\n"
                  << argv[0] << " <host> <port>\n";
        return EXIT_FAILURE;
    }

    try {
        mxnetwork::MXNetworkInit network_init;
        mx_socket_ignore_pipe_signal();
        mxnetwork::Socket sock(mxnetwork::SocketType::TYPE_INET);
        if (sock.connect(argv[1], argv[2])) {
            std::string data;
            std::cout << "enter data: ";
            std::getline(std::cin, data);
            if (sock.write_all(data.c_str(), data.length()) > 0)
                std::cout << "Sent " << data.length() << " bytes.\n";
        } else {
            std::cout << "client: Error connecting..\n";
        }
    } catch (const mxnetwork::Exception &e) {
        std::cerr << "client: " << e.text() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
