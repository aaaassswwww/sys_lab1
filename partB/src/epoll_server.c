#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int fd;
    size_t used;
    size_t sent;
    unsigned char buffer[4096];
} connection_t;

static void usage(void) {
    fprintf(stderr, "Usage: ./epoll_server --port <port> [--max-clients <n>]\n");
    exit(EXIT_FAILURE);
}

static void close_connection(int epfd, connection_t *conn) {
    if (conn->fd >= 0) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, conn->fd, NULL);
        close(conn->fd);
    }
    conn->fd = -1;
    conn->used = 0;
    conn->sent = 0;
}

static void arm_connection(int epfd, connection_t *conn, uint32_t events) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.ptr = conn;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &ev) != 0) {
        die_errno("epoll_ctl mod");
    }
}

static connection_t *allocate_connection(connection_t *conns, int max_clients, int fd) {
    int i;
    for (i = 0; i < max_clients; ++i) {
        if (conns[i].fd < 0) {
            conns[i].fd = fd;
            conns[i].used = 0;
            conns[i].sent = 0;
            return &conns[i];
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    uint16_t port = 0;
    int max_clients = 1024;
    int listen_fd;
    int epfd;
    struct epoll_event ev;
    struct epoll_event *events;
    connection_t *conns;
    int i;

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

    listen_fd = create_listen_socket(port, 256, 1);
    epfd = epoll_create1(0);
    if (epfd < 0) {
        die_errno("epoll_create1");
    }
    conns = calloc((size_t) max_clients, sizeof(*conns));
    events = calloc((size_t) max_clients + 1u, sizeof(*events));
    if (conns == NULL || events == NULL) {
        die_errno("calloc");
    }
    for (i = 0; i < max_clients; ++i) {
        conns[i].fd = -1;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.ptr = NULL;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) != 0) {
        die_errno("epoll_ctl add listen");
    }

    fprintf(stderr, "epoll_server listening on port %u\n", (unsigned) port);

    for (;;) {
        int nready = epoll_wait(epfd, events, max_clients + 1, -1);
        if (nready < 0) {
            if (errno == EINTR) {
                continue;
            }
            die_errno("epoll_wait");
        }

        for (i = 0; i < nready; ++i) {
            if (events[i].data.ptr == NULL) {
                for (;;) {
                    int client_fd = accept(listen_fd, NULL, NULL);
                    connection_t *conn;
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
                    conn = allocate_connection(conns, max_clients, client_fd);
                    if (conn == NULL) {
                        close(client_fd);
                        continue;
                    }
                    memset(&ev, 0, sizeof(ev));
                    ev.events = EPOLLIN | EPOLLRDHUP;
                    ev.data.ptr = conn;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) != 0) {
                        close_connection(epfd, conn);
                    }
                }
                continue;
            }

            {
                connection_t *conn = events[i].data.ptr;
                uint32_t revents = events[i].events;
                if (revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    close_connection(epfd, conn);
                    continue;
                }
                if ((revents & EPOLLIN) && conn->used == conn->sent) {
                    ssize_t n = read(conn->fd, conn->buffer, sizeof(conn->buffer));
                    if (n <= 0) {
                        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                            continue;
                        }
                        close_connection(epfd, conn);
                        continue;
                    }
                    conn->used = (size_t) n;
                    conn->sent = 0;
                    arm_connection(epfd, conn, EPOLLOUT | EPOLLRDHUP);
                }
                if ((revents & EPOLLOUT) && conn->sent < conn->used) {
                    ssize_t n = write(conn->fd, conn->buffer + conn->sent, conn->used - conn->sent);
                    if (n <= 0) {
                        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                            continue;
                        }
                        close_connection(epfd, conn);
                        continue;
                    }
                    conn->sent += (size_t) n;
                    if (conn->sent == conn->used) {
                        conn->used = 0;
                        conn->sent = 0;
                        arm_connection(epfd, conn, EPOLLIN | EPOLLRDHUP);
                    }
                }
            }
        }
    }
}
