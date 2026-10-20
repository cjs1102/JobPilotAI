# Benchmark Baseline

This document records a repeatable JobPilotAI performance baseline for the
open-source release. It is **reference material, not a regression threshold**.
Absolute numbers are specific to the host, kernel, CPU allocation and current
load; other machines will differ.

## 2026-10-21 release benchmark

### Environment

- OS: Ubuntu 24.04.4 LTS
- Kernel: Linux 7.0.0-38-generic, x86_64
- Virtualization: VMware; **4 online vCPUs**
- CPU: Intel Core i7-9750H
- Compiler: GCC 13.3.0; CMake 3.28.3; Conan 2.31.2
- Service: Release build (no sanitizer)
- `wrk`: 4.1.0 (debian/4.1.0-4build2, epoll)
- MySQL 8.0 and Redis 7: isolated compose project `jobpilot-bench-0023`,
  MySQL host port 14024 / Redis 17024; MySQL is on the **same host**
- Dataset: 1000 synthetic public jobs imported through the internal API
- Code baseline: C++ service from the 0022 merge `38bd484` (no C++ behavior
  change in 0023; the benchmark runner is new)
- Run date: 2026-10-21

The service binary was started from the repository root with a temporary
storage root, an internal token, and the isolated MySQL/Redis endpoints. The
benchmark runner used 5 s warmup + 10 s measured windows, 3 measured samples per
combination, and reports the median. `pidstat -u -r -p <pid> 1 10` ran during
each measured window. Raw wrk/pidstat/service logs are in
`build/benchmarks-0023/` (gitignored).

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
| Buffer append + retrieve | 1,000,000 | 10.163 | 98,394,758 |
| HTTP POST parser | 200,000 | 1,478.146 | 676,523 |
| Router parameter route | 500,000 | 265.704 | 3,763,586 |
| EventLoop cross-thread dispatch | 10,000 | 1,090.505 | 917,006 |

These values are slower than the 2026-10-06 snapshot below; the host/kernel and
build environment changed, so do not compare them as a regression without a
controlled A/B run.

### HTTP health matrix

`GET /health` with keep-alive; median of 3 measured samples. `RSS` is the peak
resident set observed by `pidstat` during the sample.

| Threads | Connections | Req/sec | p99 | %CPU | RSS |
|---:|---:|---:|---:|---:|---:|
| 2 | 32 | 10,217.76 | 9.72 ms | 89.6 | 15.8 MB |
| 2 | 128 | 10,249.06 | 33.60 ms | 90.2 | 16.1 MB |
| 2 | 512 | 8,708.87 | 148.99 ms | 92.1 | 18.6 MB |
| 4 | 32 | 9,203.14 | 10.78 ms | 90.3 | 18.6 MB |
| 4 | 128 | 11,385.51 | 28.24 ms | 88.2 | 18.6 MB |
| 4 | 512 | 10,951.73 | 98.98 ms | 92.2 | 18.6 MB |
| 8 | 32 | 11,246.85 | 9.43 ms | 85.4 | 18.6 MB |
| 8 | 128 | 12,903.95 | 24.92 ms | 86.1 | 18.6 MB |
| 8 | 512 | 9,778.91 | 118.03 ms | 90.8 | 18.6 MB |

The 4-vCPU host is CPU-saturated around 85–92% in every health row. At 512
connections the p99 rises sharply (99–149 ms), consistent with connection/CPU
pressure; this is not a product threshold. The approximated per-connection
resident-memory delta from 2 threads/32 connections (15,820 kB) to 2 threads/512
connections (18,636 kB) is
`(18,636 - 15,820) kB / 480 ≈ 5.9 kB/connection`; RSS includes shared pages and
plateaus at higher thread counts, so treat this as an order-of-magnitude
indicator, not an allocator measurement.

### DB-backed public jobs

`GET /api/v1/jobs?page=1&page_size=20`, same host MySQL; median of 3 samples.
This path includes the MySQL query and JSON rendering, so it is **not**
comparable to `/health`.

| Threads | Connections | Req/sec | p99 | %CPU | RSS |
|---:|---:|---:|---:|---:|---:|
| 2 | 32 | 128.57 | 469.24 ms | 75.2 | 22.2 MB |
| 4 | 32 | 128.43 | 408.72 ms | 73.9 | 22.2 MB |
| 8 | 32 | 117.32 | 461.19 ms | 72.0 | 22.2 MB |

The bounded result is DB/query-bound on this 4-vCPU same-host setup; adding
service threads does not improve throughput materially. The absolute p99 is
hundreds of milliseconds and is reported as observed, not as an SLA.

### Reproduce

```bash
# Start an isolated MySQL/Redis stack (project/ports/volumes) using the
# 0022 compose setup; do not point this at a developer/production database.
MYSQL_PASSWORD=... MYSQL_HOST=127.0.0.1 MYSQL_PORT=14024 MYSQL_USER=root \
MYSQL_DATABASE=jobpilot REDIS_HOST=127.0.0.1 REDIS_PORT=17024 \
INTERNAL_API_TOKEN=bench-internal-token JOBPILOT_BENCHMARK_PORT=18090 \
  backend/benchmarks/run_benchmark_matrix.sh \
  build/presets/release/service/jobpilot_service build/benchmarks-0023
```

The runner seeds 1000 synthetic jobs through `POST /internal/jobs/import`,
runs the 9 health combinations and 3 DB-backed combinations (3 samples each),
and leaves raw outputs under `build/benchmarks-0023/`. It is intentionally
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
