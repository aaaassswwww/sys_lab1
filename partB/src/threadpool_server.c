#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct client_node {
    int fd;
    struct client_node *next;
} client_node_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    client_node_t *head;
    client_node_t *tail;
} queue_t;

static void usage(void) {
    fprintf(stderr, "Usage: ./threadpool_server --port <port> [--threads <n>]\n");
    exit(EXIT_FAILURE);
}

static void queue_push(queue_t *queue, int fd) {
    client_node_t *node = malloc(sizeof(*node));
    if (node == NULL) {
        close(fd);
        return;
    }
    node->fd = fd;
    node->next = NULL;

    pthread_mutex_lock(&queue->mutex);
    if (queue->tail == NULL) {
        queue->head = queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

static int queue_pop(queue_t *queue) {
    client_node_t *node;
    int fd;

    pthread_mutex_lock(&queue->mutex);
    while (queue->head == NULL) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    node = queue->head;
    queue->head = node->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    pthread_mutex_unlock(&queue->mutex);

    fd = node->fd;
    free(node);
    return fd;
}

static void *worker_main(void *arg) {
    queue_t *queue = arg;
    unsigned char buffer[4096];

    for (;;) {
        int fd = queue_pop(queue);
        for (;;) {
            ssize_t n = read(fd, buffer, sizeof(buffer));
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                close(fd);
                break;
            }
            if (n == 0) {
                close(fd);
                break;
            }
            if (write_full(fd, buffer, (size_t) n) != n) {
                close(fd);
                break;
            }
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    uint16_t port = 0;
    int thread_count = 4;
    int listen_fd;
    queue_t queue;
    pthread_t *threads;
    int i;

    ignore_sigpipe();

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = parse_port(argv[++i]);
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            thread_count = parse_int(argv[++i], "threads", 1);
        } else {
            usage();
        }
    }
    if (port == 0) {
        usage();
    }

    memset(&queue, 0, sizeof(queue));
    if (pthread_mutex_init(&queue.mutex, NULL) != 0 ||
        pthread_cond_init(&queue.cond, NULL) != 0) {
        die("pthread init failed");
    }

    threads = calloc((size_t) thread_count, sizeof(*threads));
    if (threads == NULL) {
        die_errno("calloc");
    }
    for (i = 0; i < thread_count; ++i) {
        if (pthread_create(&threads[i], NULL, worker_main, &queue) != 0) {
            die("pthread_create failed");
        }
    }

    listen_fd = create_listen_socket(port, 256, 0);
    fprintf(stderr, "threadpool_server listening on port %u with %d threads\n",
            (unsigned) port, thread_count);

    for (;;) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            continue;
        }
        queue_push(&queue, client_fd);
    }
}
