#!/usr/bin/env bash

set -euo pipefail

HOST="${HOST:-127.0.0.1}"
BASE_PORT="${BASE_PORT:-9100}"
CONNECTIONS="${CONNECTIONS:-64}"
REQUESTS_PER_CONNECTION="${REQUESTS_PER_CONNECTION:-1000}"
PAYLOAD_SIZE="${PAYLOAD_SIZE:-1024}"
CLIENT_THREADS="${CLIENT_THREADS:-4}"
POLL_MAX_CLIENTS="${POLL_MAX_CLIENTS:-1024}"
EPOLL_MAX_CLIENTS="${EPOLL_MAX_CLIENTS:-1024}"
THREADPOOL_THREADS="${THREADPOOL_THREADS:-8}"
OUT_DIR="${OUT_DIR:-bench_results}"

mkdir -p "${OUT_DIR}"

if [[ ! -x ./load_client ]]; then
  echo "load_client not found. Run 'make' first." >&2
  exit 1
fi

run_one() {
  local name="$1"
  local port="$2"
  shift 2
  local server_cmd=("$@")
  local server_log="${OUT_DIR}/${name}_server.log"
  local client_log="${OUT_DIR}/${name}_client.log"

  echo "==> Starting ${name} on port ${port}"
  "${server_cmd[@]}" >"${server_log}" 2>&1 &
  local server_pid=$!

  sleep 1

  set +e
  ./load_client \
    --host "${HOST}" \
    --port "${port}" \
    --connections "${CONNECTIONS}" \
    --requests-per-connection "${REQUESTS_PER_CONNECTION}" \
    --payload-size "${PAYLOAD_SIZE}" \
    --threads "${CLIENT_THREADS}" \
    >"${client_log}" 2>&1
  local client_status=$?
  set -e

  kill "${server_pid}" >/dev/null 2>&1 || true
  wait "${server_pid}" 2>/dev/null || true

  if [[ ${client_status} -ne 0 ]]; then
    echo "Benchmark failed for ${name}. See ${client_log}" >&2
    return "${client_status}"
  fi
}

extract_metric() {
  local file="$1"
  local key="$2"
  awk -F'=' -v key="${key}" '
    {
      for (i = 1; i <= NF; ++i) {
        if ($i == key) {
          split($(i + 1), a, /[[:space:]]+/)
          print a[1]
          exit
        }
      }
    }
  ' "${file}"
}

write_summary() {
  local summary="${OUT_DIR}/summary.csv"
  echo "server,throughput_mib_per_sec,requests_per_sec,avg_latency_ms,p99_latency_ms" >"${summary}"

  local name
  for name in poll epoll threadpool io_uring; do
    local file="${OUT_DIR}/${name}_client.log"
    if [[ ! -f "${file}" ]]; then
      continue
    fi
    local throughput
    local rps
    local avg_latency
    local p99_latency
    throughput="$(extract_metric "${file}" "throughput_mib_per_sec")"
    rps="$(extract_metric "${file}" "requests_per_sec")"
    avg_latency="$(extract_metric "${file}" "avg_latency_ms")"
    p99_latency="$(extract_metric "${file}" "p99_latency_ms")"
    echo "${name},${throughput},${rps},${avg_latency},${p99_latency}" >>"${summary}"
  done

  echo "Summary written to ${summary}"
}

run_one "poll" "$((BASE_PORT + 1))" ./poll_server --port "$((BASE_PORT + 1))" --max-clients "${POLL_MAX_CLIENTS}"
run_one "epoll" "$((BASE_PORT + 2))" ./epoll_server --port "$((BASE_PORT + 2))" --max-clients "${EPOLL_MAX_CLIENTS}"
run_one "threadpool" "$((BASE_PORT + 3))" ./threadpool_server --port "$((BASE_PORT + 3))" --threads "${THREADPOOL_THREADS}"
run_one "io_uring" "$((BASE_PORT + 4))" ./io_uring_server --port "$((BASE_PORT + 4))"

write_summary

