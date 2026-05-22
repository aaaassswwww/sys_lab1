#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *host;
    uint16_t port;
    int connections;
    int requests_per_connection;
    size_t payload_size;
    uint64_t *latencies_ns;
    int latency_offset;
    uint64_t bytes_completed;
} worker_arg_t;

static int cmp_u64(const void *a, const void *b) {
    const uint64_t aa = *(const uint64_t *) a;
    const uint64_t bb = *(const uint64_t *) b;
    return (aa > bb) - (aa < bb);
}

static void usage(void) {
    fprintf(stderr,
            "Usage: ./load_client --host <ip> --port <port> [--connections <n>]\n"
            "                    [--requests-per-connection <n>] [--payload-size <bytes>]\n"
            "                    [--threads <n>]\n");
    exit(EXIT_FAILURE);
}

static void *worker_main(void *arg) {
    worker_arg_t *cfg = arg;
    unsigned char *send_buf = malloc(cfg->payload_size);
    unsigned char *recv_buf = malloc(cfg->payload_size);
    int conn_index;

    if (send_buf == NULL || recv_buf == NULL) {
        die_errno("malloc");
    }
    memset(send_buf, 'A', cfg->payload_size);

    for (conn_index = 0; conn_index < cfg->connections; ++conn_index) {
        int fd = connect_to_server(cfg->host, cfg->port);
        int req;
        for (req = 0; req < cfg->requests_per_connection; ++req) {
            uint64_t start = monotonic_ns();
            if (write_full(fd, send_buf, cfg->payload_size) != (ssize_t) cfg->payload_size) {
                close(fd);
                die_errno("write_full");
            }
            if (read_full(fd, recv_buf, cfg->payload_size) != (ssize_t) cfg->payload_size) {
                close(fd);
                die_errno("read_full");
            }
            cfg->latencies_ns[cfg->latency_offset + conn_index * cfg->requests_per_connection + req] =
                monotonic_ns() - start;
            cfg->bytes_completed += cfg->payload_size * 2u;
        }
        close(fd);
    }

    free(send_buf);
    free(recv_buf);
    return NULL;
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    uint16_t port = 0;
    int connections = 32;
    int requests_per_connection = 1000;
    size_t payload_size = 1024;
    int threads = 4;
    uint64_t start_ns;
    uint64_t end_ns;
    uint64_t *latencies;
    pthread_t *tids;
    worker_arg_t *args;
    int i;
    int total_requests;
    uint64_t total_bytes = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = parse_port(argv[++i]);
        } else if (strcmp(argv[i], "--connections") == 0 && i + 1 < argc) {
            connections = parse_int(argv[++i], "connections", 1);
        } else if (strcmp(argv[i], "--requests-per-connection") == 0 && i + 1 < argc) {
            requests_per_connection = parse_int(argv[++i], "requests-per-connection", 1);
        } else if (strcmp(argv[i], "--payload-size") == 0 && i + 1 < argc) {
            payload_size = parse_size(argv[++i], "payload-size", 1);
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            threads = parse_int(argv[++i], "threads", 1);
        } else {
            usage();
        }
    }
    if (port == 0) {
        usage();
    }
    if (connections < threads) {
        threads = connections;
    }

    total_requests = connections * requests_per_connection;
    latencies = calloc((size_t) total_requests, sizeof(*latencies));
    tids = calloc((size_t) threads, sizeof(*tids));
    args = calloc((size_t) threads, sizeof(*args));
    if (latencies == NULL || tids == NULL || args == NULL) {
        die_errno("calloc");
    }

    start_ns = monotonic_ns();
    {
        int base_connections = connections / threads;
        int extra = connections % threads;
        int offset = 0;
        for (i = 0; i < threads; ++i) {
            int conn_count = base_connections + (i < extra ? 1 : 0);
            args[i].host = host;
            args[i].port = port;
            args[i].connections = conn_count;
            args[i].requests_per_connection = requests_per_connection;
            args[i].payload_size = payload_size;
            args[i].latencies_ns = latencies;
            args[i].latency_offset = offset * requests_per_connection;
            offset += conn_count;
            if (pthread_create(&tids[i], NULL, worker_main, &args[i]) != 0) {
                die("pthread_create failed");
            }
        }
        for (i = 0; i < threads; ++i) {
            pthread_join(tids[i], NULL);
            total_bytes += args[i].bytes_completed;
        }
    }
    end_ns = monotonic_ns();

    qsort(latencies, (size_t) total_requests, sizeof(*latencies), cmp_u64);

    {
        double duration_sec = (double) (end_ns - start_ns) / 1e9;
        double throughput_mib = ((double) total_bytes / (1024.0 * 1024.0)) / duration_sec;
        double rps = (double) total_requests / duration_sec;
        double avg_latency_ms = 0.0;
        double p99_latency_ms;
        for (i = 0; i < total_requests; ++i) {
            avg_latency_ms += (double) latencies[i] / 1e6;
        }
        avg_latency_ms /= (double) total_requests;
        p99_latency_ms = (double) latencies[(size_t) (total_requests * 99 / 100)] / 1e6;

        printf("connections=%d requests_per_connection=%d payload_size=%zu bytes\n",
               connections, requests_per_connection, payload_size);
        printf("duration_sec=%.6f total_requests=%d total_bytes=%" PRIu64 "\n",
               duration_sec, total_requests, total_bytes);
        printf("throughput_mib_per_sec=%.3f requests_per_sec=%.3f\n", throughput_mib, rps);
        printf("avg_latency_ms=%.6f p99_latency_ms=%.6f\n", avg_latency_ms, p99_latency_ms);
    }

    free(latencies);
    free(tids);
    free(args);
    return 0;
}
