# ExecPlan 0003: Quality and Performance Baseline

> Status: Completed on 2026-10-06.
>
> Archived after sanitizer and benchmark verification.

## Background

The project has no sanitizer presets, benchmark target, CMakePresets, or CI
workflow. Current tests pass, but memory, lifetime, concurrency, and
performance risks are not measured.

## Goal

Establish safety and measurement infrastructure without changing P1 behavior:

- ASan and UBSan preset;
- separate TSan preset;
- sanitizer-aware CMake options;
- initial CTest integration;
- later Google Benchmark baseline for Buffer, HTTP parsing, routing, and event
  dispatch.

## Non-Goals

- No network behavior changes.
- No performance tuning before a baseline exists.
- No OSS-Fuzz integration in this phase.
- No broad clang-tidy/IWYU enforcement yet.

## Constraints and Invariants

- Sanitizer configuration must not weaken warnings or tests.
- ASan and TSan must remain separate.
- Benchmark must not be built as part of the normal production executable.
- External MySQL and Redis tests must still be possible.

## Research Evidence

- Current CMake has no sanitizer or benchmark targets.
- `service/build` only has Release artifacts.
- `backend/CMakeLists.txt` repeats test registration and has no shared test
  options target.
- `docs/ai/github_resources.md` recommends CMake Presets, ASan/UBSan, TSan,
  Google Benchmark, and ccache.
- CMake is 3.28.3, GCC is 13.3.0, and the verification VM has four x86-64
  vCPUs.
- `wrk` is installed at `/usr/bin/wrk`.
- Conan is 2.31.2, but `center2.conan.io` is unreachable from the current
  environment. A Conan-based Google Benchmark dependency cannot be downloaded
  or verified in this phase without weakening reproducibility claims.
- `EventLoop`, `ThreadPool`, `TcpConnection`, and `HttpServer` tests contain
  real multithreaded behavior suitable for TSan.
- MySQL, Redis, and schema tests require external services and uninstrumented
  client libraries. They remain part of ASan/UBSan coverage, but the default
  TSan gate excludes them. They will be attempted separately and reported
  explicitly rather than silently treated as TSan-clean.

## Selected Design

1. Add an interface target for sanitizer flags shared by the core library,
   tests, service executable, and vendored bcrypt.
2. Add CMake options `JP_ENABLE_ASAN`, `JP_ENABLE_UBSAN`, and
   `JP_ENABLE_TSAN`, with ASan/UBSan combinable and TSan mutually exclusive.
3. Add root and service CMake presets for debug, release, ASan, UBSan, TSan,
   and benchmark builds.
4. Run ASan/UBSan across the existing test suite.
5. Run TSan separately on EventLoop, ThreadPool, TcpConnection, TcpServer, and
   HttpServer tests. Attempt MySQL/Redis concurrency tests separately.
6. Use an isolated, dependency-free microbenchmark executable for Buffer,
   parser, router, and event-loop dispatch.
7. Use `wrk` for a repeatable HTTP `/health` throughput and latency baseline.
8. Record the exact hardware, build configuration, concurrency, request, and
   observed results under `docs/benchmarks/`.

## Rejected Alternatives

- Global compiler flags: harder to control and leak into dependencies.
- Combining ASan and TSan: unsupported and unreliable.
- Benchmarking before correctness fixes: creates a misleading baseline.
- Adding Google Benchmark through Conan now: Conan Center is unreachable in
  the current environment, so the dependency would be untested. Keep it as a
  follow-up once the remote is available.
- Fetching Google Benchmark from GitHub at configure time: reproducible builds
  would depend on a second network path and an unpinned external checkout.

## Implementation Phases

### Phase 1: Sanitizer Presets

- define interface target and options;
- add presets;
- preserve normal Release behavior;
- run ASan/UBSan CTest.

### Phase 2: TSan

- add separate TSan preset;
- run network and connection tests;
- document external-library limitations if encountered.

### Phase 3: Benchmark

- add an isolated dependency-free benchmark target;
- add a repeatable `wrk` HTTP benchmark runner;
- record Buffer, parser, router, and event-dispatch baselines;
- define regression thresholds only after stable measurements.

## Verification and Rollback

Verification:

- normal service build remains clean;
- ASan/UBSan CTest;
- TSan CTest separately;
- benchmark smoke run;
- no production targets link benchmark code.

Rollback:

- presets and optional targets can be removed without changing runtime code.

## Current Progress

- [x] Research
- [x] Phase 1: Sanitizer Presets
- [x] Phase 2: TSan
- [x] Phase 3: Benchmark

## Verification Results

- Clean `service/` and root Release configure/build: passed.
- `service/` and root Release CTest: 36/36 passed.
- ASan with leak detection: 36/36 passed.
- UBSan with non-recovering undefined behavior: 36/36 passed.
- TSan concurrency subset: 7/7 passed.
- TSan external-service subset: 6/6 passed.
- Service top-level ASan and TSan builds: passed.
- Microbenchmark: Buffer, HTTP parser, router, and event-loop dispatch measured.
- HTTP `wrk` baseline: three 10-second samples, median 32,698 requests/sec.

TSan found a real data race in `test_thread_pool.cpp`. The completion counter
was updated before `condition.notify_one()`, allowing the test to destroy the
condition variable while a worker still signaled it. The test now updates
state and signals while holding one mutex. No production network code changed.

## Known Issues and Follow-Up

- Sanitizers may expose external-library or existing-test issues; triage rather
  than suppressing.
- Benchmark thresholds should not be added before several stable runs.
- Google Benchmark remains a follow-up because Conan Center was unreachable
  during this execution.
- TSan depends on Release Conan packages because Debug package variants are not
  available in the local cache or reachable remote.
- External-service TSan tests remain separate from the default concurrency
  gate because they cross uninstrumented MySQL and Redis client libraries.
