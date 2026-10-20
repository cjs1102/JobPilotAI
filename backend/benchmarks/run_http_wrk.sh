#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <jobpilot_service-binary> [duration] [threads] [connections]" >&2
    exit 2
fi

service_binary=$1
duration=${2:-10s}
threads=${3:-2}
connections=${4:-32}
# 0023: allow the DB-backed public jobs endpoint (or another path) without
# changing existing callers. Keep the default at the original /health.
path=${5:-${JOBPILOT_BENCHMARK_PATH:-/health}}
case "$path" in
    /*) ;;
    *) echo "benchmark path must start with /: $path" >&2; exit 2 ;;
esac

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/../.." && pwd)

port=${JOBPILOT_BENCHMARK_PORT:-18080}
config_path=${JOBPILOT_CONFIG:-"$repo_root/service/config/config.yaml"}
log_path=${JOBPILOT_BENCHMARK_LOG:-/tmp/jobpilot-http-benchmark.log}

: "${MYSQL_PASSWORD:?set MYSQL_PASSWORD before running the benchmark}"

if [[ ! -x "$service_binary" ]]; then
    echo "service binary is not executable: $service_binary" >&2
    exit 2
fi

if ! command -v curl >/dev/null 2>&1; then
    echo "curl is required" >&2
    exit 2
fi

if ! command -v wrk >/dev/null 2>&1; then
    echo "wrk is required" >&2
    exit 2
fi

JOBPILOT_HOST=127.0.0.1 \
JOBPILOT_PORT="$port" \
"$service_binary" \
    --config="$config_path" \
    >"$log_path" 2>&1 &

service_pid=$!

cleanup()
{
    kill "$service_pid" >/dev/null 2>&1 || true
    wait "$service_pid" >/dev/null 2>&1 || true
}

trap cleanup EXIT INT TERM

health_url="http://127.0.0.1:${port}/health"
benchmark_url="http://127.0.0.1:${port}${path}"

for _ in $(seq 1 50); do
    if curl --fail --silent --show-error \
        "$health_url" >/dev/null 2>&1; then
        break
    fi

    if ! kill -0 "$service_pid" >/dev/null 2>&1; then
        cat "$log_path" >&2
        exit 1
    fi

    sleep 0.1
done

if ! curl --fail --silent --show-error \
    "$health_url" >/dev/null; then
    cat "$log_path" >&2
    exit 1
fi

echo "url=$benchmark_url"
echo "duration=$duration"
echo "threads=$threads"
echo "connections=$connections"

wrk \
    -t "$threads" \
    -c "$connections" \
    -d "$duration" \
    --latency \
    "$benchmark_url"
