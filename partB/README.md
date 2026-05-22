# Part B

This directory contains four echo server implementations plus a simple benchmark client:

- `poll_server`
- `epoll_server`
- `threadpool_server`
- `io_uring_server`
- `load_client`

## Build

```bash
make
```

## Run Servers

Choose one server and one port:

```bash
./poll_server --port 9001 --max-clients 1024
./epoll_server --port 9002 --max-clients 1024
./threadpool_server --port 9003 --threads 8
./io_uring_server --port 9004
```

## Run Benchmark Client

Example:

```bash
./load_client --host 127.0.0.1 --port 9001 --connections 64 --requests-per-connection 1000 --payload-size 1024 --threads 4
```

The client prints:

- total duration
- throughput in MiB/s
- requests per second
- average latency
- P99 latency

## Automated Benchmark

After `make`, you can run all four servers one by one and collect a summary:

```bash
chmod +x run_benchmarks.sh
./run_benchmarks.sh
```

Useful environment overrides:

```bash
OUT_DIR=bench_results \
CONNECTIONS=64 \
REQUESTS_PER_CONNECTION=1000 \
PAYLOAD_SIZE=1024 \
CLIENT_THREADS=4 \
./run_benchmarks.sh
```

The script writes:

- per-server stdout/stderr logs
- per-server client result logs
- `summary.csv` with throughput and latency metrics

## Paper Template

You can start the Part B write-up from:

```text
paper_template.md
```

## Notes

- `poll_server` is the baseline multiplexing implementation.
- `epoll_server` uses Linux `epoll` for better scalability than `poll`.
- `threadpool_server` uses blocking sockets and a fixed-size worker pool.
- `io_uring_server` uses the raw `io_uring` system-call interface and therefore needs a Linux kernel with `io_uring` support.
- All servers implement a simple TCP echo service.
