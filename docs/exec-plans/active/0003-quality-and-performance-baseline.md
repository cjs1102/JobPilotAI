# ExecPlan 0003: Quality and Performance Baseline

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

## Selected Design

1. Add an interface target for sanitizer flags.
2. Add CMake options such as `JP_ENABLE_ASAN`, `JP_ENABLE_UBSAN`, and
   `JP_ENABLE_TSAN`.
3. Add `CMakePresets.json` for dev, release, asan, and tsan.
4. Run ASan/UBSan first across existing tests.
5. Run TSan separately on network/concurrency tests.
6. Add Google Benchmark only after P1 correctness and P2 integration reach a
   stable point.

## Rejected Alternatives

- Global compiler flags: harder to control and leak into dependencies.
- Combining ASan and TSan: unsupported and unreliable.
- Benchmarking before correctness fixes: creates a misleading baseline.

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

- add Google Benchmark through Conan;
- add isolated benchmark target;
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

Not started.

## Known Issues and Follow-Up

- Sanitizers may expose external-library or existing-test issues; triage rather
  than suppressing.
- Benchmark thresholds should not be added before several stable runs.
