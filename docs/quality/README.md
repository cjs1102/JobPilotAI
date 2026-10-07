# Quality Baseline

This directory records the repeatable sanitizer and quality commands for
JobPilotAI.

Equivalent presets exist in both the repository root and `service/`. Run the
commands from either supported build entry after generating the corresponding
Conan toolchain.

## Dependency Boundary

The current environment cannot reach Conan Center, and the local cache only
contains Release dependency packages. The sanitizer presets therefore compile
project sources with `-O1 -g` while linking the already-verified Release
Conan packages. They do not pretend to provide Debug dependency builds.

Normal tests keep assertions active because backend test targets append
`-UNDEBUG` after the Release `-DNDEBUG` flag.

## ASan

```bash
cmake --preset asan
cmake --build --preset asan -j"$(nproc)"
MYSQL_PASSWORD=root \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  ctest --preset asan
```

Scope: all 60 current tests, including live MySQL, Redis, schema, and User
vertical-slice checks.

## UBSan

```bash
cmake --preset ubsan
cmake --build --preset ubsan -j"$(nproc)"
MYSQL_PASSWORD=root \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --preset ubsan
```

Scope: all 60 current tests and `-fno-sanitize-recover=undefined`.

## TSan

```bash
cmake --preset tsan
cmake --build --preset tsan -j"$(nproc)"
MYSQL_PASSWORD=root TSAN_OPTIONS=halt_on_error=1 \
  ctest --preset tsan
MYSQL_PASSWORD=root TSAN_OPTIONS=halt_on_error=1 \
  ctest --preset tsan-external
```

The default TSan gate covers:

- EventLoop cross-thread dispatch and eventfd wakeup;
- ThreadPool submission, completion, exception handling, and shutdown;
- TcpConnection read, write, callback, and socket lifecycle behavior;
- TcpServer request/connection integration;
- HttpServer request concurrency and response ordering.

GCC TSan on this Ubuntu VM can fail during process startup with
`unexpected memory mapping` when ASLR is enabled. The CMake targets set
`CROSSCOMPILING_EMULATOR` to `setarch <arch> -R` only for TSan builds, so
discovery and test execution use a stable address layout. This is a runtime
environment workaround, not a test suppression.

MySQL, Redis, and schema tests are kept in the separate `tsan-external` preset
because they cross uninstrumented client-library boundaries. They passed in
the recorded baseline, but are intentionally not part of the default TSan gate.

## Recorded Baseline

Date: 2026-10-07

| Check | Result |
|---|---|
| ASan full CTest | 60/60 passed |
| UBSan full CTest | 60/60 passed |
| TSan concurrency/User tests | 21/21 passed |
| TSan external-service tests | 6/6 passed |
| Focused Release regression | 9/9 passed |

TSan initially found a race in `test_thread_pool.cpp`: the completion counter
was updated before `condition.notify_one()` while the test could leave scope
and destroy the condition variable. The fix updates completion state and
signals while holding the same mutex. No P1 production code was changed.
