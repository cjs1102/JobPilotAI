# Benchmark Baseline

This document records a repeatable JobPilotAI performance baseline for the
open-source release. It is **reference material, not a regression threshold**.
Absolute numbers are specific to the host, kernel, CPU allocation and current
load; other machines will differ.

## 2026-10-21 release benchmark (exclusive rerun)

### Environment and window

- OS: Ubuntu 24.04.4 LTS
- Kernel: Linux 7.0.0-38-generic, x86_64
- Virtualization: VMware; **4 online vCPUs**
- CPU: Intel Core i7-9750H
- Compiler: GCC 13.3.0; CMake 3.28.3; Conan 2.31.2
- Service: Release build (no sanitizer)
- `wrk`: 4.1.0 (debian/4.1.0-4build2, epoll)
- MySQL 8.0 and Redis 7: isolated compose project `jobpilot-bench-0023x`,
  MySQL host port 14025 / Redis 17025; MySQL is on the **same host**
- Dataset: 1000 synthetic public jobs imported through the internal API
- Code baseline: C++ service from the 0022 merge `38bd484` (no C++ behavior
  change in 0023; the benchmark runner is new)
- Run date: 2026-10-21; exclusive window **00:38:57–00:48:41** local time
- `nproc`: 4
- Load before microbenchmark: `0.69 1.28 2.36`; before matrix: `0.95 1.32 2.37`
- Load immediately after the matrix: `4.36 3.70 3.06` (wrk/service load; no
  other build, test, or benchmark was started during the run)
- CPU governor: unavailable from this VM (no readable
  `/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`); frequency/thermal
  state could not be pinned or recorded

The service binary was started from the repository root with a temporary
storage root, an internal token, and the isolated MySQL/Redis endpoints. The
benchmark runner used 5 s warmup + 10 s measured windows, 3 measured samples per
combination, and reports the median. `pidstat -u -r -p <pid> 1 10` ran during
each measured window. Raw wrk/pidstat/service logs are in
`build/benchmarks-0023-exclusive/` (gitignored), together with
`environment-before.txt` and `environment-after.txt`.

### Microbenchmarks

The dependency-free microbenchmark target was rebuilt from the same tree:

```bash
cmake --preset benchmark
cmake --build --preset benchmark -j"$(nproc)" --target jobpilot_microbench
./build/presets/benchmark/backend/jobpilot_microbench
```

Each row is the median of five measured runs after warmups.

| Benchmark | Iterations | Median ns/op | Operations/sec |
|---|---:|---:|---:|
| Buffer append + retrieve | 1,000,000 | 11.109 | 90,019,510 |
| HTTP POST parser | 200,000 | 1,778.914 | 562,141 |
| Router parameter route | 500,000 | 292.337 | 3,420,706 |
| EventLoop cross-thread dispatch | 10,000 | 1,217.963 | 821,043 |

### HTTP health matrix

`GET /health` with keep-alive; median of 3 measured samples. `RSS` is the peak
resident set observed by `pidstat` during the sample.

| Threads | Connections | Req/sec | p99 | %CPU | RSS |
|---:|---:|---:|---:|---:|---:|
| 2 | 32 | 13,421.82 | 7.17 ms | 91.6 | 15.2 MB |
| 2 | 128 | 16,129.71 | 18.45 ms | 90.7 | 15.5 MB |
| 2 | 512 | 13,182.93 | 84.16 ms | 96.7 | 17.9 MB |
| 4 | 32 | 13,694.17 | 7.15 ms | 91.8 | 17.9 MB |
| 4 | 128 | 13,808.09 | 22.41 ms | 97.1 | 16.9 MB |
| 4 | 512 | 14,455.12 | 68.49 ms | 96.2 | 16.9 MB |
| 8 | 32 | 13,865.30 | 7.06 ms | 91.8 | 17.0 MB |
| 8 | 128 | 13,256.94 | 20.82 ms | 92.1 | 17.0 MB |
| 8 | 512 | 12,344.01 | 77.78 ms | 95.9 | 16.9 MB |

The 4-vCPU host is CPU-saturated around 90–97% in every health row. At 512
connections the p99 rises (68–84 ms), consistent with connection/CPU pressure;
this is not a product threshold. The approximated per-connection resident-memory
delta from 2 threads/32 connections (15,596 kB) to 2 threads/512 connections
(18,284 kB) is `(18,284 - 15,596) kB / 480 ≈ 5.6 kB/connection`; RSS includes
shared pages and plateaus at higher thread counts, so treat this as an
order-of-magnitude indicator, not an allocator measurement.

### DB-backed public jobs

`GET /api/v1/jobs?page=1&page_size=20`, same host MySQL; median of 3 samples.
This path includes the MySQL query and JSON rendering, so it is **not**
comparable to `/health`.

| Threads | Connections | Req/sec | p99 | %CPU | RSS |
|---:|---:|---:|---:|---:|---:|
| 2 | 32 | 180.40 | 347.83 ms | 84.3 | 20.4 MB |
| 4 | 32 | 182.47 | 255.66 ms | 82.5 | 20.4 MB |
| 8 | 32 | 193.81 | 223.74 ms | 81.0 | 20.6 MB |

The bounded result is DB/query-bound on this 4-vCPU same-host setup; adding
service threads does not improve throughput materially. The absolute p99 is
hundreds of milliseconds and is reported as observed, not as an SLA.

### Comparison with the 2026-10-06 baseline

The historical health baseline was about **32,698 req/sec** at 2 threads/32
connections. The exclusive rerun is **13,421.82 req/sec** at the same shape,
about **2.4× lower**; the microbenchmarks are also roughly 2× slower. This is
recorded as a difference, not hidden or normalized:

- The old baseline has no committed raw log, service commit, `pidstat` output or
  exact binary attribution; only the summary table exists. It cannot be treated
  as a controlled A/B against today's binary.
- The host kernel moved from `7.0.0-34-generic` to `7.0.0-38-generic`; CPU
  frequency/thermal state is not readable and the governor could not be pinned.
- The pre-run load was not zero (`0.69` 1-minute on a 4-vCPU VM), and the
  service saturates 90–97% CPU in every health row; the old run may have had a
  quieter host.
- The old 32.7k number was a single health shape without `pidstat`; the new
  matrix adds 5 s warmup, `pidstat` sampling and 3 samples. Those overheads are
  small relative to 2.4×, but they are not byte-identical methods.
- MySQL/Redis containers and an idle developer stack share the same 4-vCPU host;
  the old run's container/load state is not recorded.

The exclusive rerun is the release baseline for this document. The old values
remain below as historical reference; do not claim them as a current contract or
use them to assert a product regression without a same-session A/B.

### Reproduce

```bash
# Start an isolated MySQL/Redis stack (project/ports/volumes); do not point
# this at a developer/production database.
MYSQL_PASSWORD=... MYSQL_HOST=127.0.0.1 MYSQL_PORT=14025 MYSQL_USER=root \
MYSQL_DATABASE=jobpilot REDIS_HOST=127.0.0.1 REDIS_PORT=17025 \
INTERNAL_API_TOKEN=bench-internal-token JOBPILOT_BENCHMARK_PORT=18091 \
  backend/benchmarks/run_benchmark_matrix.sh \
  build/presets/release/service/jobpilot_service build/benchmarks-0023-exclusive
```

The runner seeds 1000 synthetic jobs through `POST /internal/jobs/import`,
runs the 9 health combinations and 3 DB-backed combinations (3 samples each),
and leaves raw outputs under the chosen output directory. It is intentionally
exclusive: do not run it concurrently with L4/CI or another load generator.

## Previous baseline (historical, 2026-10-06)

The first baseline was produced on the same VM before later changes. It is kept
for history; its absolute values are not a current contract.

### Microbenchmarks (historical)

| Benchmark | Iterations | Median ns/op | Operations/sec |
|---|---:|---:|---:|
| Buffer append + retrieve | 1,000,000 | 5.465 | 182,985,865 |
| HTTP POST parser | 200,000 | 737.176 | 1,356,529 |
| Router parameter route | 500,000 | 132.537 | 7,545,063 |
| EventLoop cross-thread dispatch | 10,000 | 442.656 | 2,259,091 |

### HTTP baseline (historical)

`GET /health`, `wrk -t2 -c32 -d10s`; the three samples were 31,209.88,
32,698.15 and 32,759.06 req/sec, median ≈ 32,698 req/sec. The environment and
build were different from the 2026-10-21 matrix; the old numbers are
**historical/unverified for the current commit**.

## Interpretation rules

- Treat all numbers as this-host observations; do not promise them across
  machines or CI runners.
- A performance change needs a controlled A/B run on the same host, with the
  same binary/build configuration and load; this document is not that harness.
- `pidstat` CPU/RSS is sampled once per second and rounded; per-connection
  memory is approximate.
- The benchmark does not test automatic submission, AI providers, or external
  network sources; it is a local service/DB baseline.
