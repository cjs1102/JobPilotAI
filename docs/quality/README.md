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

## Open-source MVP Quality Gates

- Core tests must pass without an LLM or external AI provider.
- Source-adapter tests use fixtures and recorded/synthetic payloads; live
  provider checks are opt-in.
- AI provider tests mock network boundaries and verify timeout, degradation,
  invalid output, and retry behavior.
- Provider keys are never required for normal CI and never printed.
- The documented local flow must remain runnable with MySQL and Redis only;
  AI is optional.
- 0011 ranking tests must verify deterministic ordering, score versioning,
  hard-filter/ranking separation, and no double penalty.
- 0012 adapter tests must cover complete observation, partial pagination,
  timeout, rate limit, source failure, disappearance grace, and reopen.
- 0013 worker tests must verify atomic claim, lease timeout, retry, stale
  recovery, provider failure, and rule-only fallback.
- A release/demo gate should eventually cover:
  `clone -> configure -> start -> import/discover -> search -> match -> track`.

## Source Adapter Tests

The Python adapters use recorded fixtures and never contact a live ATS API in
the suite:

```bash
cd source_adapters
python3 -m unittest discover -s jobpilot_sources/tests -t .
```

Coverage: provider fixture parsing (Greenhouse/Lever/Ashby), completeness
classification (Greenhouse total mismatch; a board above the 200-job limit
becomes incomplete), partial pagination, HTTP timeout, rate limit and source
failure, duplicate source URLs, config validation, "token is never logged", and
a guard that the package imports no database driver and contains no SQL.

An opt-in live smoke (not part of CI) replaces fixtures with real public APIs:

```bash
INTERNAL_API_TOKEN=dummy python3 -m jobpilot_sources sync \
  --config /path/to/live-config.yaml --dry-run
```

## ASan

```bash
cmake --preset asan
cmake --build --preset asan -j"$(nproc)"
MYSQL_PASSWORD=root \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  ctest --preset asan
```

Scope: all 115 current tests, including live MySQL, Redis, schema, User,
Resume, Job, Job Ingestion, Job Ranking, and Application checks.

## UBSan

```bash
cmake --preset ubsan
cmake --build --preset ubsan -j"$(nproc)"
MYSQL_PASSWORD=root \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --preset ubsan
```

Scope: all 115 current tests and `-fno-sanitize-recover=undefined`.

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
- HttpServer request concurrency and response ordering;
- Job ingestion transaction rollback and HTTP idempotency;
- Application service duplicate-create concurrency and transaction behavior.

GCC TSan on this Ubuntu VM can fail during process startup with
`unexpected memory mapping` when ASLR is enabled. The CMake targets set
`CROSSCOMPILING_EMULATOR` to `setarch <arch> -R` only for TSan builds, so
discovery and test execution use a stable address layout. This is a runtime
environment workaround, not a test suppression.

MySQL, Redis, and schema tests are kept in the separate `tsan-external` preset
because they cross uninstrumented client-library boundaries. They passed in
the recorded baseline, but are intentionally not part of the default TSan gate.

## Recorded Baseline

Date: 2026-10-08

0013 L4 results, 230 discovered tests per preset, run once at stage close
against HEAD `f81b0cd` (see `docs/ai/agent-workflow.md` section 2 for when a
level may run):

| Check | Result |
|---|---|
| Debug full CTest | 230/230 passed |
| ASan full CTest | 230/230 passed (no sanitizer diagnostics) |
| UBSan full CTest | 230/230 passed (no sanitizer diagnostics) |
| Release full CTest | 230/230 passed |
| Python source adapter suite | 85/85 passed |
| Python ai_worker suite | 190/190 passed |
| TSan default gate | 42/42 passed (last concurrency baseline, after 0010) |
| TSan external-service tests | 6/6 passed (same baseline) |
| Service and root Release builds | passed |

TSan was not rerun for 0011 or 0012 because neither slice introduced new
concurrency, thread, event-loop, connection-lifetime, or background-worker
behavior. The 42/42 TSan baseline remains recorded for the last
concurrency-relevant stage.

TSan initially found a race in `test_thread_pool.cpp`: the completion counter
was updated before `condition.notify_one()` while the test could leave scope
and destroy the condition variable. The fix updates completion state and
signals while holding the same mutex. No P1 production code was changed.

## 0013 AI Task Tests

The 0013 gate in this document requires worker tests to verify atomic claim,
lease timeout, retry, stale recovery, provider failure, and rule-only fallback.
Mapping to executed tests (release preset, HEAD 3b326a3):

| Gate item | Proof |
|---|---|
| atomic claim | `AiTaskDaoTest.NeverHandsTheSameTaskToTwoWorkers` — three threads over four tasks, every task claimed exactly once |
| lease timeout | `AiTaskDaoTest.CompletionRequiresOwnerAndLiveLease`, `AiTaskHttpTest.RefusesInputWithoutALiveLease` |
| retry | `AiTaskDaoTest.RequeuesRetryableFailureBelowTheCap`, `AiTaskDaoTest.FailsPermanentFailureImmediately`, `AiTaskHttpTest.RetryableFailRequeuesAndPermanentFailTerminates` |
| stale recovery | `AiTaskDaoTest.ReclaimsExpiredLeaseForAnotherWorker`, `AiTaskDaoTest.FailsExpiredLeaseBeyondAttemptCap` |
| provider failure | `ai_worker` provider tests (timeout / 429 / 5xx / invalid JSON / missing key) plus the live cross-language E2E failure path (stub returns 500 -> worker calls `fail(retryable=true)` -> the task returns to pending with `error_msg` set) |
| rule-only fallback | `MatchRuleTest` (determinism, recency excluded), the service degradation tests, the `ai_worker` merge tests (the score can never be overwritten) and the live E2E (`status=2` with `ai.degraded=true`) |

Commands:

```bash
cd service && ctest --test-dir build/presets/release -L ai                     # 32 tests
MYSQL_PASSWORD=<pwd> ./build/presets/release/test_ai_task_dao                  # 15 tests
./build/presets/release/test_match_rule                                        # 6 tests
cd ../ai_worker && python3 -m unittest discover -s jobpilot_ai/tests -t .      # 190 tests
cd ../source_adapters && python3 -m unittest discover -s jobpilot_sources/tests -t .  # 85 tests
```

The cross-language contract is additionally proven by a live run of the worker
against the real service binary (`claim -> input -> complete`, `rule_result`
byte-identical, score preserved, `ai.degraded` marked). That run is reproducible
scratch, not a committed test; the permanent proof is the HTTP lifecycle test in
`test_ai_http.cpp` plus the worker's own stub-driven tests.
