#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int fd;
    size_t used;
    size_t sent;
    unsigned char buffer[4096];
} connection_t;

static void usage(void) {
    fprintf(stderr, "Usage: ./poll_server --port <port> [--max-clients <n>]\n");
    exit(EXIT_FAILURE);
}

static void close_connection(struct pollfd *pfd, connection_t *conn) {
    if (conn->fd >= 0) {
        close(conn->fd);
    }
    pfd->fd = -1;
    pfd->events = 0;
    pfd->revents = 0;
    conn->fd = -1;
    conn->used = 0;
    conn->sent = 0;
}

static int add_connection(struct pollfd *pfds, connection_t *conns, int capacity, int fd) {
    int i;
    for (i = 1; i <= capacity; ++i) {
        if (conns[i].fd < 0) {
            conns[i].fd = fd;
            conns[i].used = 0;
            conns[i].sent = 0;
            pfds[i].fd = fd;
            pfds[i].events = POLLIN;
            pfds[i].revents = 0;
            return 0;
        }
    }
    return -1;
}

int main(int argc, char **argv) {
    uint16_t port = 0;
    int max_clients = 1024;
    int listen_fd;
    int i;
    struct rlimit rlim;
    struct pollfd *pfds;
    connection_t *conns;

    ignore_sigpipe();

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = parse_port(argv[++i]);
        } else if (strcmp(argv[i], "--max-clients") == 0 && i + 1 < argc) {
            max_clients = parse_int(argv[++i], "max-clients", 1);
        } else {
            usage();
        }
    }
    if (port == 0) {
        usage();
    }

    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        die_errno("getrlimit");
    }
    if ((rlim_t) max_clients + 1u >= rlim.rlim_cur) {
        if (rlim.rlim_cur <= 2) {
            die("RLIMIT_NOFILE is too small for poll_server");
        }
        max_clients = (int) rlim.rlim_cur - 2;
        fprintf(stderr, "poll_server adjusted max-clients to %d due to RLIMIT_NOFILE\n", max_clients);
    }

    listen_fd = create_listen_socket(port, 256, 1);
    pfds = calloc((size_t) max_clients + 1u, sizeof(*pfds));
    conns = calloc((size_t) max_clients + 1u, sizeof(*conns));
    if (pfds == NULL || conns == NULL) {
        die_errno("calloc");
    }

    pfds[0].fd = listen_fd;
    pfds[0].events = POLLIN;
    for (i = 1; i <= max_clients; ++i) {
        pfds[i].fd = -1;
        conns[i].fd = -1;
    }

    fprintf(stderr, "poll_server listening on port %u\n", (unsigned) port);

    for (;;) {
        int ready = poll(pfds, (nfds_t) max_clients + 1u, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            die_errno("poll");
        }

        if (pfds[0].revents & POLLIN) {
            for (;;) {
                int client_fd = accept(listen_fd, NULL, NULL);
                if (client_fd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    if (errno == EINTR) {
                        continue;
                    }
                    die_errno("accept");
                }
                set_nonblocking(client_fd);
                if (add_connection(pfds, conns, max_clients, client_fd) != 0) {
                    close(client_fd);
                }
            }
        }

        for (i = 1; i <= max_clients; ++i) {
            connection_t *conn = &conns[i];
            if (conn->fd < 0 || pfds[i].revents == 0) {
                continue;
            }
            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close_connection(&pfds[i], conn);
                continue;
            }
            if ((pfds[i].revents & POLLIN) && conn->used == conn->sent) {
                ssize_t n = read(conn->fd, conn->buffer, sizeof(conn->buffer));
                if (n <= 0) {
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        continue;
                    }
                    close_connection(&pfds[i], conn);
                    continue;
                }
                conn->used = (size_t) n;
                conn->sent = 0;
                pfds[i].events = POLLOUT;
            }
            if ((pfds[i].revents & POLLOUT) && conn->sent < conn->used) {
                ssize_t n = write(conn->fd, conn->buffer + conn->sent, conn->used - conn->sent);
                if (n <= 0) {
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        continue;
                    }
                    close_connection(&pfds[i], conn);
                    continue;
                }
                conn->sent += (size_t) n;
                if (conn->sent == conn->used) {
                    conn->used = 0;
                    conn->sent = 0;
                    pfds[i].events = POLLIN;
                }
            }
        }
    }
}
