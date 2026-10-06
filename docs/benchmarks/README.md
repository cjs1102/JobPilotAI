# Benchmark Baseline

This document records the first repeatable JobPilotAI performance baseline.
It is a reference point, not a regression threshold.

## Environment

- OS: Ubuntu 24.04.4 LTS
- Kernel: Linux 7.0.0-34-generic
- Architecture: x86_64
- Virtualization: VMware
- CPU: Intel Core i7-9750H, 4 online vCPUs
- Compiler: GCC 13.3.0
- CMake: 3.28.3
- Conan: 2.31.2
- Build preset: `benchmark`
- Build configuration: Release

All results were produced on the same VM. Other machines, CPU allocation, and
background load will produce different absolute numbers.

## Microbenchmarks

The microbenchmark target is intentionally dependency-free so it does not
require an unavailable network package:

```bash
cmake --preset benchmark
cmake --build --preset benchmark -j"$(nproc)"
cmake --build --preset benchmark --target run_microbench
```

Each row is the median of five measured runs after three warmups. The event-loop
scenario measures cross-thread `QueueInLoop`, eventfd wakeup, pending-functor
dispatch, and thread join.

| Benchmark | Iterations | Median ns/op | Operations/sec |
|---|---:|---:|---:|
| Buffer append + retrieve | 1,000,000 | 5.465 | 182,985,865 |
| HTTP POST parser | 200,000 | 737.176 | 1,356,529 |
| Router parameter route | 500,000 | 132.537 | 7,545,063 |
| EventLoop cross-thread dispatch | 10,000 | 442.656 | 2,259,091 |

## HTTP Baseline

The HTTP baseline runs the Release service on an isolated port. The request is
`GET /health HTTP/1.1` with keep-alive.

```bash
MYSQL_PASSWORD=root \
  JOBPILOT_BENCHMARK_PORT=18080 \
  backend/benchmarks/run_http_wrk.sh \
  build/presets/benchmark/service/jobpilot_service \
  10s 2 32
```

Configuration:

- `wrk` threads: 2
- connections: 32
- duration: 10 seconds per sample
- response body: static health JSON

| Sample | Requests/sec | Average latency | p50 | p99 |
|---|---:|---:|---:|---:|
| 1 | 31,209.88 | 1.02 ms | 0.90 ms | 2.50 ms |
| 2 | 32,698.15 | 0.98 ms | 0.85 ms | 2.31 ms |
| 3 | 32,759.06 | 0.97 ms | 0.85 ms | 2.28 ms |

Median throughput: approximately 32,698 requests/sec.

The first sample is retained in the report even though it is lower than the
later warm samples. No pass/fail threshold is set until repeated runs on a
stable and less contended environment are available.
