#include "relaywindow.hpp"
#include <QApplication>
#include <iostream>

int main(int argc, char *argv[]) {

    if (argc != 4) {
        std::cerr << "Error use:\n"
                  << argv[0] << " <host> <port> <username>\n";
        return EXIT_FAILURE;
    }

    QApplication app(argc, argv);
    RelayWindow window;
    if (!window.makeConnection(argv[3], argv[1], argv[2])) {
        std::cerr << "Error making connection to: " << argv[1] << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "made connection to: " << argv[1] << "\n";
    window.setWindowTitle("Connected to: " + QString(argv[1]) + " as: " + argv[3]);
    window.show();
    return app.exec();
}
