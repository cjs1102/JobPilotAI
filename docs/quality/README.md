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

## Verification Levels

Use the smallest level that covers the changed behavior. Escalate when the
change crosses into a higher-risk boundary.

- **L1 local change**: build only affected targets; run directly related
  tests. Do not run full CTest or sanitizers.
- **L2 business-module change**: incremental build; run affected DAO, Service,
  Controller, or module tests. Add relevant HTTP/integration tests when the
  behavior crosses that boundary. Sanitizers are not the default.
- **L3 infrastructure/high-risk change**: HTTP parser, Router, EventLoop,
  Poller, Connection, thread/concurrency, lifetime, or protocol/serialization
  boundaries require their focused regression tests. Run TSan only when the
  change exercises concurrency, and run ASan/UBSan when the risk requires
  them.
- **L4 stage completion**: run full Debug CTest, full ASan, and full UBSan.
  Run TSan when concurrency was touched and Release build when the stage
  changes release-relevant build behavior.
- **L5 pre-commit**: choose the minimum sufficient verification for the final
  diff. If L4 passed after the last relevant code change, reuse that evidence
  and do not repeat the full matrix. If relevant files changed afterward,
  rerun the affected checks and repeat the invalidated L4 gates.

Documentation-only changes do not require a rebuild unless generated or
runtime behavior depends on the documentation.

Record the command, scope, result count, failures, and environment. On success,
report a concise summary instead of replaying the complete log. On failure,
preserve the first causal error and the relevant failing output.

## ASan

```bash
cmake --preset asan
cmake --build --preset asan -j"$(nproc)"
MYSQL_PASSWORD=root \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  ctest --preset asan
```

Scope: all 65 current tests, including live MySQL, Redis, schema, and User
vertical-slice checks.

## UBSan

```bash
cmake --preset ubsan
cmake --build --preset ubsan -j"$(nproc)"
MYSQL_PASSWORD=root \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --preset ubsan
```

Scope: all 65 current tests and `-fno-sanitize-recover=undefined`.

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
| ASan full CTest | 65/65 passed |
| UBSan full CTest | 65/65 passed |
| TSan concurrency/User tests | 21/21 passed |
| TSan external-service tests | 6/6 passed |
| Focused Release regression | 9/9 passed |

TSan initially found a race in `test_thread_pool.cpp`: the completion counter
was updated before `condition.notify_one()` while the test could leave scope
and destroy the condition variable. The fix updates completion state and
signals while holding the same mutex. No P1 production code was changed.
