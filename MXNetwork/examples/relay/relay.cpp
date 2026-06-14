#include "mxnetwork/socket.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <string>
#include <thread>
#include <vector>

std::atomic<bool> active{false};
std::mutex mut;
static constexpr size_t BUFFER_SIZE = 1024 * 8;

class Relay {
  public:
    Relay() {
        std::cout << "relay: init.\n";
        mx_socket_ignore_pipe_signal();
    }
    ~Relay() {
        std::cout << "relay: quit.\n";
    }
    void stop() {
        active.store(false);
    }
    bool listen(std::string_view port) {
        if (sockfd.listen(port, 5)) {
            active.store(true);
            std::vector<mxnetwork::Socket> sockets;
            Messages message(&sockets);
            std::thread background(std::move(message));
            while (active.load()) {
                std::optional<mxnetwork::Socket> new_s = sockfd.accept();
                if (new_s == std::nullopt) {
                    if (errno == EINTR)
                        continue;
                    active.store(false);
                    continue;
                }
                mut.lock();
                new_s->setblocking(false);
                sockets.push_back(std::move(*new_s));
                mut.unlock();
            }
            background.join();
        } else {
            perror("listen");
            return false;
        }
        return true;
    }

  private:
    mxnetwork::Socket sockfd{mxnetwork::SocketType::TYPE_INET};
    class Messages {
      public:
        Messages(std::vector<mxnetwork::Socket> *s) : sockets(s) {}
        void operator()() {
            while (active.load()) {
                std::vector<pollfd> p_fd;
                {
                    std::lock_guard<std::mutex> lock(mut);
                    p_fd.reserve(sockets->size());
                    for (const auto &s : *sockets) {
                        pollfd pfd{};
                        pfd.fd = s.sockfd();
                        pfd.events = POLLIN;
                        p_fd.push_back(pfd);
                    }
                }

                if (p_fd.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                int value = poll(p_fd.data(), p_fd.size(), 100);
                if (value <= 0)
                    continue;
                std::lock_guard<std::mutex> lock(mut);
                if (p_fd.size() != sockets->size())
                    continue;
                for (size_t i = 0; i < p_fd.size(); ++i) {
                    if (p_fd[i].revents & POLLIN) {
                        char buffer[BUFFER_SIZE] = {};
                        ssize_t bytes = (*sockets)[i].read(buffer, BUFFER_SIZE - 1, 0);
                        if (bytes < 0) {
                            if (errno != EAGAIN || errno != EWOULDBLOCK) {
                                if (!sockets->empty())
                                    (*sockets)[i].close();
                            }
                        } else if (bytes == 0) {
                            (*sockets)[i].close();
                            std::cout << "relay: Disconnected safely.\n";
                        } else {
                            buffer[bytes] = 0;
                            std::cout << "relay: Got message: " << buffer << "\n";
                            send_all(i, buffer, bytes);
                        }
                    }
                }
                std::erase_if(*sockets, [](const mxnetwork::Socket &s) {
                    bool dead = s.socket_type() == mxnetwork::SocketType::TYPE_INVALID;
                    if (dead) {
                        std::cout << "relay: removing dead socket.\n";
                    }
                    return dead;
                });
            }
        }

        void send_all(size_t i, const char *buffer, size_t bytes) {
            for (size_t z = 0; z < sockets->size(); ++z) {
                if (i != z) {
                    ssize_t b = (*sockets)[z].write(buffer, bytes, 0);
                    if (b == -1) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
                            if (!sockets->empty())
                                (*sockets)[z].close();
                        continue;
                    }
                    std::cout << "relay: Sent message to: " << (*sockets)[z].sockfd() << " " << buffer << "\n";
                }
            }
        }

        std::vector<mxnetwork::Socket> *sockets;
    };
};

void quit_signal(int) {
    active.store(false);
}

int main(int argc, char **argv) {

    if (argc != 2) {
        std::cout << "Use:\n"
                  << argv[0] << " <port>\n";
        return EXIT_FAILURE;
    }
    try {

        struct sigaction sa{};
        sa.sa_handler = quit_signal;
        if (sigaction(SIGINT, &sa, nullptr) == -1) {
            perror("sigaction");
            return EXIT_FAILURE;
        }

        Relay relay;
        relay.listen(argv[1]);
        return EXIT_SUCCESS;
    } catch (const mxnetwork::Exception &e) {
        std::cerr << "relay: Exception: " << e.text() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
