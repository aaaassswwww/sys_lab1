#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

void die(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

void die_errno(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

void ignore_sigpipe(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) != 0) {
        die_errno("sigaction");
    }
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        die_errno("fcntl(F_GETFL)");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        die_errno("fcntl(F_SETFL)");
    }
}

int create_listen_socket(uint16_t port, int backlog, int nonblocking) {
    int fd;
    int one = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die_errno("socket");
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
        close(fd);
        die_errno("setsockopt(SO_REUSEADDR)");
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (const struct sockaddr *) &addr, sizeof(addr)) != 0) {
        close(fd);
        die_errno("bind");
    }
    if (listen(fd, backlog) != 0) {
        close(fd);
        die_errno("listen");
    }
    if (nonblocking) {
        set_nonblocking(fd);
    }
    return fd;
}

int connect_to_server(const char *host, uint16_t port) {
    char port_text[16];
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    int fd = -1;

    snprintf(port_text, sizeof(port_text), "%u", (unsigned) port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_text, &hints, &result) != 0) {
        die("getaddrinfo failed");
    }

    for (it = result; it != NULL; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);

    if (fd < 0) {
        die_errno("connect");
    }
    return fd;
}

uint16_t parse_port(const char *text) {
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (text[0] == '\0' || (end != NULL && *end != '\0') || value > 65535ul) {
        die("invalid port");
    }
    return (uint16_t) value;
}

int parse_int(const char *text, const char *name, int min_value) {
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (text[0] == '\0' || (end != NULL && *end != '\0') || value < min_value) {
        fprintf(stderr, "invalid %s\n", name);
        exit(EXIT_FAILURE);
    }
    return (int) value;
}

size_t parse_size(const char *text, const char *name, size_t min_value) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (text[0] == '\0' || (end != NULL && *end != '\0') || value < min_value) {
        fprintf(stderr, "invalid %s\n", name);
        exit(EXIT_FAILURE);
    }
    return (size_t) value;
}

uint64_t monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        die_errno("clock_gettime");
    }
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

ssize_t write_full(int fd, const void *buffer, size_t length) {
    const unsigned char *p = buffer;
    size_t done = 0;
    while (done < length) {
        ssize_t n = write(fd, p + done, length - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            break;
        }
        done += (size_t) n;
    }
    return (ssize_t) done;
}

ssize_t read_full(int fd, void *buffer, size_t length) {
    unsigned char *p = buffer;
    size_t done = 0;
    while (done < length) {
        ssize_t n = read(fd, p + done, length - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            break;
        }
        done += (size_t) n;
    }
    return (ssize_t) done;
}
