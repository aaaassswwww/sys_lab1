#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/io_uring.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef struct {
    unsigned *head;
    unsigned *tail;
    unsigned *ring_mask;
    unsigned *ring_entries;
    unsigned *flags;
    unsigned *array;
    struct io_uring_sqe *sqes;
    void *sq_ptr;
    size_t sq_sz;

    unsigned *cq_head;
    unsigned *cq_tail;
    unsigned *cq_ring_mask;
    unsigned *cq_ring_entries;
    struct io_uring_cqe *cqes;
    void *cq_ptr;
    size_t cq_sz;

    int ring_fd;
} ring_t;

typedef struct {
    int fd;
    unsigned char buffer[4096];
    size_t length;
} connection_t;

typedef enum {
    OP_ACCEPT = 1,
    OP_RECV = 2,
    OP_SEND = 3
} op_type_t;

typedef struct request {
    op_type_t type;
    int fd;
    connection_t *conn;
    struct sockaddr_in addr;
    socklen_t addr_len;
} request_t;

static int io_uring_setup_wrap(unsigned entries, struct io_uring_params *params) {
    return (int) syscall(SYS_io_uring_setup, entries, params);
}

static int io_uring_enter_wrap(int fd, unsigned to_submit, unsigned min_complete, unsigned flags) {
    return (int) syscall(SYS_io_uring_enter, fd, to_submit, min_complete, flags, NULL, 0);
}

static void ring_init(ring_t *ring, unsigned entries) {
    struct io_uring_params params;
    size_t sq_off = 0;
    size_t cq_off = 0;

    memset(&params, 0, sizeof(params));
    ring->ring_fd = io_uring_setup_wrap(entries, &params);
    if (ring->ring_fd < 0) {
        die_errno("io_uring_setup");
    }

    ring->sq_sz = params.sq_off.array + params.sq_entries * sizeof(unsigned);
    ring->cq_sz = params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);
    if (params.features & IORING_FEAT_SINGLE_MMAP) {
        if (ring->cq_sz > ring->sq_sz) {
            ring->sq_sz = ring->cq_sz;
        } else {
            ring->cq_sz = ring->sq_sz;
        }
    }

    ring->sq_ptr = mmap(NULL, ring->sq_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                        ring->ring_fd, IORING_OFF_SQ_RING);
    if (ring->sq_ptr == MAP_FAILED) {
        die_errno("mmap sq");
    }

    if (params.features & IORING_FEAT_SINGLE_MMAP) {
        ring->cq_ptr = ring->sq_ptr;
    } else {
        ring->cq_ptr = mmap(NULL, ring->cq_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                            ring->ring_fd, IORING_OFF_CQ_RING);
        if (ring->cq_ptr == MAP_FAILED) {
            die_errno("mmap cq");
        }
    }

    ring->sqes = mmap(NULL, params.sq_entries * sizeof(struct io_uring_sqe),
                      PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                      ring->ring_fd, IORING_OFF_SQES);
    if (ring->sqes == MAP_FAILED) {
        die_errno("mmap sqes");
    }

    sq_off = params.sq_off.head;
    ring->head = (unsigned *) ((char *) ring->sq_ptr + sq_off);
    ring->tail = (unsigned *) ((char *) ring->sq_ptr + params.sq_off.tail);
    ring->ring_mask = (unsigned *) ((char *) ring->sq_ptr + params.sq_off.ring_mask);
    ring->ring_entries = (unsigned *) ((char *) ring->sq_ptr + params.sq_off.ring_entries);
    ring->flags = (unsigned *) ((char *) ring->sq_ptr + params.sq_off.flags);
    ring->array = (unsigned *) ((char *) ring->sq_ptr + params.sq_off.array);

    ring->cq_head = (unsigned *) ((char *) ring->cq_ptr + params.cq_off.head);
    ring->cq_tail = (unsigned *) ((char *) ring->cq_ptr + params.cq_off.tail);
    ring->cq_ring_mask = (unsigned *) ((char *) ring->cq_ptr + params.cq_off.ring_mask);
    ring->cq_ring_entries = (unsigned *) ((char *) ring->cq_ptr + params.cq_off.ring_entries);
    ring->cqes = (struct io_uring_cqe *) ((char *) ring->cq_ptr + params.cq_off.cqes);
}

static struct io_uring_sqe *ring_get_sqe(ring_t *ring) {
    unsigned tail = *ring->tail;
    if (tail - *ring->head >= *ring->ring_entries) {
        return NULL;
    }
    return &ring->sqes[tail & *ring->ring_mask];
}

static void submit_request(ring_t *ring, request_t *req) {
    struct io_uring_sqe *sqe;
    for (;;) {
        sqe = ring_get_sqe(ring);
        if (sqe != NULL) {
            break;
        }
        if (io_uring_enter_wrap(ring->ring_fd, 0, 1, IORING_ENTER_GETEVENTS) < 0) {
            die_errno("io_uring_enter wait");
        }
    }

    memset(sqe, 0, sizeof(*sqe));
    sqe->user_data = (uint64_t) (uintptr_t) req;

    if (req->type == OP_ACCEPT) {
        req->addr_len = sizeof(req->addr);
        sqe->opcode = IORING_OP_ACCEPT;
        sqe->fd = req->fd;
        sqe->addr = (uint64_t) (uintptr_t) &req->addr;
        sqe->addr2 = (uint64_t) (uintptr_t) &req->addr_len;
    } else if (req->type == OP_RECV) {
        sqe->opcode = IORING_OP_RECV;
        sqe->fd = req->fd;
        sqe->addr = (uint64_t) (uintptr_t) req->conn->buffer;
        sqe->len = sizeof(req->conn->buffer);
    } else {
        sqe->opcode = IORING_OP_SEND;
        sqe->fd = req->fd;
        sqe->addr = (uint64_t) (uintptr_t) req->conn->buffer;
        sqe->len = (unsigned) req->conn->length;
    }

    ring->array[*ring->tail & *ring->ring_mask] = *ring->tail & *ring->ring_mask;
    (*ring->tail)++;
    if (io_uring_enter_wrap(ring->ring_fd, 1, 0, 0) < 0) {
        die_errno("io_uring_enter submit");
    }
}

static request_t *make_request(op_type_t type, int fd, connection_t *conn) {
    request_t *req = calloc(1, sizeof(*req));
    if (req == NULL) {
        die_errno("calloc");
    }
    req->type = type;
    req->fd = fd;
    req->conn = conn;
    return req;
}

static void close_connection(connection_t *conn) {
    if (conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
        conn->length = 0;
    }
}

static void usage(void) {
    fprintf(stderr, "Usage: ./io_uring_server --port <port>\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    uint16_t port = 0;
    int listen_fd;
    ring_t ring;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = parse_port(argv[++i]);
        } else {
            usage();
        }
    }
    if (port == 0) {
        usage();
    }

    ignore_sigpipe();
    listen_fd = create_listen_socket(port, 256, 0);
    ring_init(&ring, 256);
    submit_request(&ring, make_request(OP_ACCEPT, listen_fd, NULL));

    fprintf(stderr, "io_uring_server listening on port %u\n", (unsigned) port);

    for (;;) {
        unsigned head = *ring.cq_head;
        if (head == *ring.cq_tail) {
            if (io_uring_enter_wrap(ring.ring_fd, 0, 1, IORING_ENTER_GETEVENTS) < 0) {
                die_errno("io_uring_enter wait");
            }
            continue;
        }

        while (head != *ring.cq_tail) {
            struct io_uring_cqe *cqe = &ring.cqes[head & *ring.cq_ring_mask];
            request_t *req = (request_t *) (uintptr_t) cqe->user_data;
            int result = cqe->res;

            if (req->type == OP_ACCEPT) {
                submit_request(&ring, make_request(OP_ACCEPT, listen_fd, NULL));
                if (result >= 0) {
                    connection_t *conn = calloc(1, sizeof(*conn));
                    if (conn != NULL) {
                        conn->fd = result;
                        submit_request(&ring, make_request(OP_RECV, conn->fd, conn));
                    } else {
                        close(result);
                    }
                }
            } else if (req->type == OP_RECV) {
                connection_t *conn = req->conn;
                if (result <= 0) {
                    close_connection(conn);
                    free(conn);
                } else {
                    conn->length = (size_t) result;
                    submit_request(&ring, make_request(OP_SEND, conn->fd, conn));
                }
            } else if (req->type == OP_SEND) {
                connection_t *conn = req->conn;
                if (result <= 0) {
                    close_connection(conn);
                    free(conn);
                } else {
                    submit_request(&ring, make_request(OP_RECV, conn->fd, conn));
                }
            }

            free(req);
            ++head;
        }
        *ring.cq_head = head;
    }
}
