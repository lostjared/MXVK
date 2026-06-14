// I need to reimplement this with more C++ style using string instead of raw pointers
#include "mxnetwork/socket.hpp"
#include <cstdlib>
#include <errno.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

static constexpr size_t BUFFER_SIZE = 4096;

struct http_header {
    char *header, *body;
    size_t body_length;
};

bool extract_header(mxnetwork::Socket *sock, struct http_header *h) {
    size_t buffer_size_value = BUFFER_SIZE;
    size_t length = 0;
    char *buffer = (char *)malloc(BUFFER_SIZE + 1);
    ssize_t rbytes = 0;
    char data[1024];
    data[0] = 0;
    while (1) {
        rbytes = sock->read(data, sizeof(data), 0);
        if (rbytes <= 0) {
            fprintf(stderr, "Connection closed: Error.\n");
            free(buffer);
            return false;
        }
        if (length + (size_t)rbytes >= buffer_size_value) {
            buffer_size_value *= 2;
            char *n_buffer = (char *)realloc(buffer, buffer_size_value + 1);
            if (n_buffer == nullptr) {
                perror("realloc");
                free(buffer);
                return false;
            }
            buffer = n_buffer;
        }
        memcpy(buffer + length, data, (size_t)rbytes);
        length += (size_t)rbytes;
        buffer[length] = '\0';
        auto loc = strstr(buffer, "\r\n\r\n");
        if (loc != nullptr) {
            ssize_t pos = loc - buffer;
            h->header = (char *)malloc((size_t)pos + 1);
            if (h->header == nullptr) {
                free(buffer);
                perror("malloc");
                return false;
            }
            memcpy(h->header, buffer, (size_t)pos);
            h->header[pos] = '\0';
            size_t body_start = (size_t)pos + 4;
            h->body_length = length - body_start;
            if (h->body_length > 0) {
                h->body = (char *)malloc(h->body_length);
                if (h->body == nullptr) {
                    perror("malloc");
                    free(h->header);
                    free(buffer);
                    return false;
                }
                memcpy(h->body, buffer + body_start, h->body_length);
            } else {
                h->body = nullptr;
            }
            free(buffer);
            return true;
        }
    }
    return true;
}

static int get_terminal_width() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        return 80;
    }
    return w.ws_col;
}

static void print_progress(size_t length, size_t progress) {

    if (length == 0) {
        printf("\r\033[2K[ Downloading ] - 0%%\n");
        return;
    }

    if (progress > length) {
        progress = length;
    }
    int term_width = get_terminal_width();
    int bar_width = term_width - 12;
    double ratio = (double)progress / (double)length;
    int num_equals = (int)(ratio * bar_width);
    int percent = (int)(ratio * 100.0);
    if (bar_width < 10)
        bar_width = 10;
    printf("\r\033[2K[");
    for (int j = 0; j < bar_width; ++j) {
        if (j < num_equals) {
            putchar('=');
        } else {
            putchar(' ');
        }
    }
    printf("] - %d%%", percent);
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Error on invoke of dl-file:\nuse:\ndl <host> <port> <file> <output>\n");
        return EXIT_FAILURE;
    }

    try {
        mx_socket_ignore_pipe_signal();
        mxnetwork::Socket sock(mxnetwork::SocketType::TYPE_INET);
        if (sock.connect(argv[1], argv[2])) {
            printf("dl: connected.\n");
            static constexpr int buffer_size = 4096;
            char info[buffer_size];
            snprintf(info, buffer_size - 1, "GET %s HTTP/1.0\r\nHOST: %s\r\nUser-Agent: C-DL-Client/1.0\r\nConnection: close\r\n\r\n", argv[3], argv[1]);
            ssize_t bytes = 0;
            if ((bytes = sock.write(info, strlen(info), MSG_NOSIGNAL)) > 0) {
                printf("dl: request sent.\n");
                struct http_header h;
                if (extract_header(&sock, &h)) {
                    printf("Header: %s\n", h.header);
                    char *len_pos = strcasestr(h.header, "Content-Length:");
                    if (len_pos != nullptr) {
                        len_pos += 15;
                        char *end_pos;
                        size_t f_len = strtoul(len_pos, &end_pos, 10);
                        if (end_pos != len_pos && (end_pos != nullptr && *end_pos != '\0')) {
                            FILE *fptr = fopen(argv[4], "wb");
                            if (!fptr) {
                                perror("fopen");
                                free(h.header);
                                free(h.body);
                                return EXIT_FAILURE;
                            }
                            size_t file_length = 0;
                            if (h.body_length > 0) {
                                file_length += fwrite(h.body, 1, h.body_length, fptr);
                                print_progress(f_len, file_length);
                            }
                            while ((bytes = sock.read(info, buffer_size, 0)) > 0) {
                                file_length += fwrite(info, 1, (size_t)bytes, fptr);
                                print_progress(f_len, file_length);
                            }
                            if (bytes == -1) {
                                fprintf(stderr, "dl: Connection reset or error: %s\n", strerror(errno));
                            } else {
                                printf("\n");
                                if (file_length == f_len)
                                    printf("dl: [OK] -> %s\n", argv[4]);
                                else
                                    printf("Incorrect file length: %zu != %zu\n", file_length, f_len);
                            }
                            fclose(fptr);
                        }
                    }
                    free(h.header);
                    free(h.body);
                }
            } else if (bytes == -1 && errno == EPIPE) {
                fprintf(stderr, "dl: Error on send, broken pipe.\n");
            }
            sock.close();
        } else {
            fprintf(stderr, "Error on connect:\n");
            return EXIT_FAILURE;
        }
    } catch (const mxnetwork::Exception &e) {
        std::cerr << "Network error: " << e.text() << "\n";
    }
    return EXIT_SUCCESS;
}
