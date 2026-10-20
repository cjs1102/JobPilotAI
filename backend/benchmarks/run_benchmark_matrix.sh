#!/usr/bin/env bash
# 0023 release-material benchmark matrix.
#
# This runner assumes an ALREADY-ISOLATED MySQL/Redis stack (see the ExecPlan):
# it starts one release service process, seeds 1000 synthetic jobs through the
# internal import API, then runs:
#
#   health: threads 2/4/8 x connections 32/128/512, 3 measured samples each
#   jobs:   threads 2/4/8 x connections 32, 3 measured samples each
#
# Each sample is 5 s warmup + 10 s measured; pidstat records CPU/RSS during the
# measured window. Raw wrk/pidstat/service logs are written under OUT_DIR; this
# script does not touch a developer stack and does not create MySQL/Redis.
#
# Usage:
#   MYSQL_PASSWORD=... MYSQL_PORT=14024 REDIS_PORT=17024 \
#     backend/benchmarks/run_benchmark_matrix.sh \
#     build/presets/release/service/jobpilot_service [out_dir]
set -euo pipefail

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$R"

SERVICE="${1:-build/presets/release/service/jobpilot_service}"
OUT="${2:-build/benchmarks-0023}"
PORT="${JOBPILOT_BENCHMARK_PORT:-18090}"
DURATION="${JOBPILOT_BENCHMARK_DURATION:-10}"
SAMPLES="${JOBPILOT_BENCHMARK_SAMPLES:-3}"
INTERNAL_TOKEN="${INTERNAL_API_TOKEN:-bench-internal-token}"
MYSQL_HOST="${MYSQL_HOST:-127.0.0.1}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_USER="${MYSQL_USER:-root}"
MYSQL_DATABASE="${MYSQL_DATABASE:-jobpilot}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"
: "${MYSQL_PASSWORD:?set MYSQL_PASSWORD for the isolated benchmark DB}"

mkdir -p "$OUT/tmp" "$OUT/storage"
: > "$OUT/matrix.csv"
printf '%s\n' 'scenario,threads,connections,sample,requests_per_sec,latency_avg,p99,cpu_percent,rss_kb' > "$OUT/matrix.csv"

[ -x "$SERVICE" ] || { echo "service binary is not executable: $SERVICE" >&2; exit 2; }
command -v wrk >/dev/null || { echo "wrk is required" >&2; exit 2; }
command -v pidstat >/dev/null || { echo "pidstat is required" >&2; exit 2; }
command -v curl >/dev/null || { echo "curl is required" >&2; exit 2; }
python3 -c 'import json' || { echo "python3 is required" >&2; exit 2; }

service_pid=""
cleanup() {
    local rc=$?
    if [ -n "$service_pid" ]; then
        kill "$service_pid" >/dev/null 2>&1 || true
        wait "$service_pid" >/dev/null 2>&1 || true
    fi
    rm -rf "$OUT/tmp"
    exit "$rc"
}
trap cleanup EXIT INT TERM

echo "benchmark service: $SERVICE"
echo "out: $OUT"
echo "port: $PORT; samples: $SAMPLES; measured duration: ${DURATION}s"
echo "MySQL: ${MYSQL_HOST}:${MYSQL_PORT}/${MYSQL_DATABASE} (same host)"
echo "wrk: $(wrk --version 2>&1 | head -1)"

JOBPILOT_HOST=127.0.0.1 \
JOBPILOT_PORT="$PORT" \
MYSQL_HOST="$MYSQL_HOST" MYSQL_PORT="$MYSQL_PORT" MYSQL_USER="$MYSQL_USER" \
MYSQL_PASSWORD="$MYSQL_PASSWORD" MYSQL_DATABASE="$MYSQL_DATABASE" \
REDIS_HOST="$REDIS_HOST" REDIS_PORT="$REDIS_PORT" \
INTERNAL_API_TOKEN="$INTERNAL_TOKEN" \
STORAGE_ROOT="$OUT/storage" \
"$SERVICE" --config=service/config/config.yaml > "$OUT/service.log" 2>&1 &
service_pid=$!

BASE="http://127.0.0.1:${PORT}"
for _ in $(seq 1 60); do
    if curl -fsS --max-time 2 "$BASE/health" >/dev/null 2>&1; then
        break
    fi
    if ! kill -0 "$service_pid" >/dev/null 2>&1; then
        cat "$OUT/service.log" >&2
        exit 1
    fi
    sleep 0.5
done
curl -fsS --max-time 2 "$BASE/health" >/dev/null || {
    cat "$OUT/service.log" >&2
    exit 1
}
echo "service health: $BASE/health"

echo "seeding 1000 synthetic jobs"
python3 - "$OUT/tmp" <<'PY'
import json
import sys
from pathlib import Path

out = Path(sys.argv[1])
for batch in range(10):
    jobs = []
    for offset in range(100):
        index = batch * 100 + offset
        jobs.append({
            "company": "Benchmark Company %02d" % (index % 20),
            "title": "C++ Backend Engineer %04d" % index,
            "location": "Shenzhen" if index % 2 == 0 else "Shanghai",
            "salary_min": 20 + (index % 10),
            "salary_max": 35 + (index % 10),
            "salary_currency": "CNY",
            "description": "Benchmark job %d for the public jobs endpoint." % index,
            "source": "import",
            "source_url": "https://bench.example.invalid/jobs/%d" % index,
            "requirements": [
                {"skill": "C++", "level": 3},
                {"skill": "Linux", "level": 2},
            ],
        })
    (out / ("batch-%02d.json" % batch)).write_text(
        json.dumps({"jobs": jobs}, ensure_ascii=False),
        encoding="utf-8",
    )
PY

for batch in "$OUT"/tmp/batch-*.json; do
    code="$(curl -sS -o "$OUT/import-last.json" -w '%{http_code}' -X POST \
        "$BASE/internal/jobs/import" \
        -H 'Content-Type: application/json' \
        -H "X-Internal-Token: $INTERNAL_TOKEN" \
        -d @"$batch")"
    [ "$code" -lt 400 ] || { cat "$OUT/import-last.json" >&2; exit 1; }
done
echo "seed import: 10 x 100 jobs"

parse_pidstat() {
    local file="$1"
    python3 - "$file" <<'PY'
import sys
from pathlib import Path

cpu = []
rss = []
for line in Path(sys.argv[1]).read_text(errors="replace").splitlines():
    parts = line.split()
    if not parts or not parts[0][:2].isdigit():
        continue
    try:
        if len(parts) >= 10:
            # pidstat -u row: time UID PID %usr %system %guest %wait %CPU CPU Command
            cpu.append(float(parts[7]))
        elif len(parts) == 9:
            # pidstat -r row: time UID PID minflt majflt VSZ RSS %MEM Command
            rss.append(int(parts[6]))
    except ValueError:
        continue
if cpu:
    print("%.2f" % (sum(cpu) / len(cpu)), max(rss) if rss else "NA")
else:
    print("NA", "NA")
PY
}

run_sample() {
    local scenario="$1" threads="$2" connections="$3" sample="$4" path="$5"
    local stem="${scenario}-t${threads}-c${connections}-s${sample}"
    local url="$BASE$path"
    local wrk_file="$OUT/${stem}.wrk.txt"
    local pid_file="$OUT/${stem}.pidstat.txt"

    # Warmup is deliberately not measured.
    wrk -t "$threads" -c "$connections" -d 5s "$url" > "$OUT/${stem}.warmup.txt" 2>&1
    pidstat -u -r -p "$service_pid" 1 "$DURATION" > "$pid_file" 2>&1 &
    local pidstat_pid=$!
    wrk -t "$threads" -c "$connections" -d "${DURATION}s" --latency "$url" > "$wrk_file" 2>&1
    wait "$pidstat_pid" || true

    local rps lat p99 cpu rss
    rps="$(awk '/Requests\/sec:/{print $2}' "$wrk_file")"
    lat="$(awk '/Latency/{print $2; exit}' "$wrk_file")"
    p99="$(awk '/99%/{print $2; exit}' "$wrk_file")"
    read -r cpu rss < <(parse_pidstat "$pid_file")
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$scenario" "$threads" "$connections" "$sample" "$rps" "$lat" "$p99" "$cpu" "$rss" \
        >> "$OUT/matrix.csv"
    echo "  $stem: rps=${rps:-NA} p99=${p99:-NA} cpu=${cpu}% rss=${rss}kB"
}

for threads in 2 4 8; do
    for connections in 32 128 512; do
        for sample in $(seq 1 "$SAMPLES"); do
            run_sample health "$threads" "$connections" "$sample" /health
        done
    done
done

for threads in 2 4 8; do
    for sample in $(seq 1 "$SAMPLES"); do
        run_sample jobs "$threads" 32 "$sample" '/api/v1/jobs?page=1&page_size=20'
    done
done

echo "matrix: $OUT/matrix.csv"
echo "raw wrk/pidstat logs: $OUT/"
echo "BENCHMARK_MATRIX_OK"
