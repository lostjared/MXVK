// generated example for library MXNetwork
#include "mxnetwork/socket.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/socket.h>
#endif

namespace {

    constexpr unsigned int port_min = 1024;
    constexpr unsigned int port_max = 4951;
    constexpr size_t buffer_size = 4096;
    constexpr int progress_width = 48;

    std::atomic<bool> active_loop{false};
    std::atomic<mxnetwork::Socket *> listen_socket{nullptr};

    void request_stop() {
        active_loop.store(false);
        mxnetwork::Socket *listener = listen_socket.load();
        if (listener == nullptr) {
            return;
        }

        const mx_socket_fd fd = listener->sockfd();
        if (fd != NULL_SOCKET) {
#ifdef _WIN32
            shutdown(fd, SD_BOTH);
#else
            shutdown(fd, SHUT_RDWR);
#endif
        }
        listener->close();
    }

    bool send_text(mxnetwork::Socket &sock, std::string_view text) {
        return sock.write_all(text.data(), text.size()) == static_cast<ssize_t>(text.size());
    }

    void send_error(mxnetwork::Socket &sock, std::string_view message) {
        if (!send_text(sock, message)) {
            std::cerr << "fileserve: Error could not send data.\n";
        }
    }

    bool valid_filename(std::string_view filename) {
        if (filename.empty() || filename == "." || filename == "..") {
            return false;
        }
        if (filename.find('/') != std::string_view::npos || filename.find('\\') != std::string_view::npos) {
            return false;
        }

        const std::filesystem::path path{std::string(filename)};
        return !path.is_absolute() && !path.has_parent_path() && path.filename() == path;
    }

    void list_directory(mxnetwork::Socket &sock) {
        std::error_code error;
        for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(".", error)) {
            if (error) {
                std::cerr << "fileserve: Error reading directory: " << error.message() << "\n";
                send_error(sock, "error: 100\r\n");
                return;
            }
            if (!entry.is_regular_file(error) || error) {
                error.clear();
                continue;
            }

            const std::string filename = entry.path().filename().string() + "\n";
            if (!send_text(sock, filename)) {
                std::cerr << "fileserve: could not write data.\n";
                return;
            }
        }

        if (!send_text(sock, "\r\n")) {
            std::cerr << "fileserve: could not write data.\n";
        }
    }

    void send_file(mxnetwork::Socket &sock, std::string_view filename) {
        if (!valid_filename(filename)) {
            std::cerr << "fileserve: Error invalid path.\n";
            send_error(sock, "error: 101\r\n");
            return;
        }

        std::filesystem::path path{std::string(filename)};
        std::error_code error;
        const uintmax_t file_size = std::filesystem::file_size(path, error);
        if (error) {
            std::cerr << "fileserve: Could not stat file: " << error.message() << "\n";
            send_error(sock, "error: 103\r\n");
            return;
        }
        if (file_size > static_cast<uintmax_t>(std::numeric_limits<size_t>::max())) {
            std::cerr << "fileserve: File too large.\n";
            send_error(sock, "error: 104\r\n");
            return;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::cerr << "fileserve: Could not open file: " << filename << "\n";
            send_error(sock, "error: 103\r\n");
            return;
        }

        if (!send_text(sock, "Content-Length: " + std::to_string(file_size) + "\n")) {
            std::cerr << "fileserve: Error could not write header.\n";
            return;
        }

        std::vector<char> buffer(buffer_size);
        while (file) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize bytes = file.gcount();
            if (bytes <= 0) {
                break;
            }
            if (sock.write_all(buffer.data(), static_cast<size_t>(bytes)) != bytes) {
                std::cerr << "fileserve: Error could not write file data.\n";
                return;
            }
        }
    }

    std::optional<std::string> read_command(mxnetwork::Socket &sock) {
        std::string command;
        std::vector<char> buffer(buffer_size);

        while (active_loop.load()) {
            const ssize_t bytes = sock.read(buffer.data(), buffer.size(), 0);
            if (bytes <= 0) {
                return std::nullopt;
            }

            command.append(buffer.data(), static_cast<size_t>(bytes));
            const size_t end = command.find("\r\n");
            if (end != std::string::npos) {
                command.resize(end);
                return command;
            }
            if (command.size() > buffer_size) {
                send_error(sock, "error: 102\r\n");
                return std::nullopt;
            }
        }

        return std::nullopt;
    }

    void process_input(mxnetwork::Socket sock) {
        while (active_loop.load()) {
            std::optional<std::string> command = read_command(sock);
            if (!command) {
                break;
            }

            std::cout << "fileserve: command: " << *command << "\n";
            if (*command == "ls") {
                list_directory(sock);
            } else if (command->starts_with("get: ")) {
                const std::string_view filename{command->data() + 5, command->size() - 5};
                std::cout << "fileserve: sending file: " << filename << "\n";
                send_file(sock, filename);
            } else if (*command == "exit:") {
                request_stop();
                break;
            } else {
                std::cout << "fileserve: invalid command.\n";
                send_error(sock, "error: 102\r\n");
            }
        }

        std::cout << "Exiting thread socket[" << sock.sockfd() << "]\n";
    }

#ifdef _WIN32
    BOOL WINAPI console_handler(DWORD signal) {
        if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
            request_stop();
            return TRUE;
        }
        return FALSE;
    }
#else
    void listen_signal(int) {
        active_loop.store(false);
    }
#endif

    void listen_server(std::string_view port) {
        mxnetwork::Socket sock(mxnetwork::SocketType::TYPE_INET);
        if (!sock.listen(port, 5)) {
            std::cerr << "file_serve: Error on listen..\n";
            return;
        }
        if (!sock.setblocking(true)) {
            std::cerr << "file_serve: Error setting listener blocking mode.\n";
            return;
        }

        std::cout << "file_serve: Listening on port " << port << "\n";
        active_loop.store(true);
        listen_socket.store(&sock);
        while (active_loop.load()) {
            std::optional<mxnetwork::Socket> new_socket = sock.accept();
            if (!new_socket) {
                if (active_loop.load()) {
                    std::cerr << "fileserve: accept failed.\n";
                }
                continue;
            }

            std::thread thread(process_input, std::move(*new_socket));
            thread.detach();
        }

        listen_socket.store(nullptr);
        std::cout << "fileserve: Exiting...\n";
    }

    void print_progress(int &prev, size_t length, size_t progress) {
        if (length == 0) {
            std::cout << "\r[ Downloading ] - 0%" << std::flush;
            return;
        }
        if (progress > length) {
            progress = length;
        }

        const double ratio = static_cast<double>(progress) / static_cast<double>(length);
        const int percent = static_cast<int>(ratio * 100.0);
        if (prev == percent) {
            return;
        }
        prev = percent;

        const int filled = static_cast<int>(ratio * progress_width);
        std::cout << "\r[";
        for (int i = 0; i < progress_width; ++i) {
            std::cout << (i < filled ? '=' : ' ');
        }
        std::cout << "] - " << percent << "%" << std::flush;
    }

    std::optional<size_t> parse_content_length(std::string_view data) {
        constexpr std::string_view prefix = "Content-Length: ";
        if (!data.starts_with(prefix)) {
            return std::nullopt;
        }

        const size_t line_end = data.find('\n');
        if (line_end == std::string_view::npos) {
            return std::nullopt;
        }

        size_t length = 0;
        const std::string_view value = data.substr(prefix.size(), line_end - prefix.size());
        const auto result = std::from_chars(value.data(), value.data() + value.size(), length);
        if (result.ec != std::errc() || result.ptr != value.data() + value.size()) {
            return std::nullopt;
        }
        return length;
    }

    std::optional<std::string> parse_get_filename(std::string_view command) {
        constexpr std::string_view prefix = "get: ";
        if (!command.starts_with(prefix)) {
            return std::nullopt;
        }

        std::string filename{command.substr(prefix.size())};
        if (!valid_filename(filename)) {
            return std::nullopt;
        }
        return filename;
    }

    void receive_listing(mxnetwork::Socket &sock) {
        std::string output;
        std::vector<char> buffer(buffer_size);
        while (true) {
            const ssize_t bytes = sock.read(buffer.data(), buffer.size(), 0);
            if (bytes <= 0) {
                break;
            }

            output.append(buffer.data(), static_cast<size_t>(bytes));
            const size_t end = output.find("\r\n");
            if (end != std::string::npos) {
                std::cout << output.substr(0, end);
                break;
            }
        }
    }

    void receive_file(mxnetwork::Socket &sock, std::string_view command) {
        const std::optional<std::string> filename = parse_get_filename(command);
        if (!filename) {
            std::cerr << "fileserve: Error on interpreting get command.\n";
            return;
        }

        std::vector<char> buffer(buffer_size);
        const ssize_t bytes = sock.read(buffer.data(), buffer.size(), 0);
        if (bytes <= 0) {
            return;
        }

        std::string header_and_data(buffer.data(), static_cast<size_t>(bytes));
        if (header_and_data.starts_with("error:")) {
            std::cerr << "fileserve: Server returned " << header_and_data << "\n";
            return;
        }

        const std::optional<size_t> file_size = parse_content_length(header_and_data);
        const size_t data_start = header_and_data.find('\n');
        if (!file_size || data_start == std::string::npos) {
            std::cerr << "fileserve: Invalid server response.\n";
            return;
        }

        std::ofstream file(*filename, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::cerr << "fileserve: Could not open output file: " << *filename << "\n";
            return;
        }

        size_t bytes_written = 0;
        const size_t initial_offset = data_start + 1;
        if (initial_offset < header_and_data.size()) {
            const size_t initial_bytes = std::min(*file_size, header_and_data.size() - initial_offset);
            file.write(header_and_data.data() + initial_offset, static_cast<std::streamsize>(initial_bytes));
            bytes_written += initial_bytes;
        }

        int prev = -1;
        print_progress(prev, *file_size, bytes_written);
        while (bytes_written < *file_size) {
            const size_t to_read = std::min(buffer.size(), *file_size - bytes_written);
            const ssize_t read_bytes = sock.read(buffer.data(), to_read, 0);
            if (read_bytes <= 0) {
                std::cerr << "\nfileserve: Error reading file data.\n";
                break;
            }

            file.write(buffer.data(), read_bytes);
            if (!file) {
                std::cerr << "\nfileserve: Error writing to file.\n";
                break;
            }
            bytes_written += static_cast<size_t>(read_bytes);
            print_progress(prev, *file_size, bytes_written);
        }

        print_progress(prev, *file_size, bytes_written);
        std::cout << "\nfileserve: Saved " << bytes_written << " bytes to " << *filename << "\n";
    }

    void connect_client(std::string_view host, std::string_view port) {
        active_loop.store(true);
        mxnetwork::Socket sock(mxnetwork::SocketType::TYPE_INET);
        if (!sock.connect(host, port)) {
            std::cerr << "fileserve: Error could not connect\n";
            return;
        }

        while (active_loop.load()) {
            std::string input;
            std::cout << "fileserve> ";
            if (!std::getline(std::cin, input)) {
                break;
            }

            const std::string command = input + "\r\n";
            if (!send_text(sock, command)) {
                std::cerr << "fserve: Error could not write data.\n";
                break;
            }

            if (input == "exit:") {
                break;
            }
            if (input == "help") {
                std::cout << "program commands:\nhelp\t[this message]\nls\t[list files]\nget: <filename>\t[get file]\nexit:\t[exit program.]\n";
            } else if (input == "ls") {
                receive_listing(sock);
            } else if (input.starts_with("get: ")) {
                receive_file(sock, input);
            } else {
                std::cout << "fileserve: invalid command.\n";
            }
        }
    }

    bool parse_port(const char *value, unsigned int &port) {
        const std::string_view text{value};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), port);
        return result.ec == std::errc() && result.ptr == text.data() + text.size();
    }

    bool validate_port(const char *program, const char *value, unsigned int &port) {
        if (!parse_port(value, port)) {
            std::cerr << "fileserve: Error use:\n"
                      << program << " <port>\n";
            return false;
        }
        if (port < port_min || port > port_max) {
            std::cerr << "fileserve: Error port out of range.\nUse: " << port_min << "-" << port_max << " (suggested: 3000)\n";
            return false;
        }
        return true;
    }

} // namespace

int main(int argc, char **argv) {
    mxnetwork::MXNetworkInit network_init;
    mx_socket_ignore_pipe_signal();

#ifdef _WIN32
    if (!SetConsoleCtrlHandler(console_handler, TRUE)) {
        std::cerr << "fileserve: Could not install console handler.\n";
        return EXIT_FAILURE;
    }
#else
    struct sigaction sa = {};
    sa.sa_handler = listen_signal;
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        std::cerr << "fileserve: Could not install signal handler.\n";
        return EXIT_FAILURE;
    }
#endif

    unsigned int port = 0;
    try {
        if (argc == 2) {
            if (!validate_port(argv[0], argv[1], port)) {
                return EXIT_FAILURE;
            }
            listen_server(argv[1]);
        } else if (argc == 3) {
            if (!validate_port(argv[0], argv[2], port)) {
                return EXIT_FAILURE;
            }
            std::cout << "Connecting...\n";
            connect_client(argv[1], argv[2]);
        } else {
            std::cerr << "Error use:\n"
                      << argv[0] << " <port>\t\tfor listen (server)\n"
                      << argv[0] << " <host> <port>\tfor connect (client)\n";
            return EXIT_SUCCESS;
        }
    } catch (const mxnetwork::Exception &error) {
        std::cerr << "fileserve: " << error.text() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "fileserve: Exiting.\n";
    return EXIT_SUCCESS;
}
