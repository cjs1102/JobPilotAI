# JobPilotAI Next-Phase Plan

> Date: 2026-10-06
>
> Planning baseline:
>
> - `HEAD = 2be52122c73acc6897dfdb8d235346e4c6dd7da9`
> - `origin/main = 2be52122c73acc6897dfdb8d235346e4c6dd7da9`
>
> The user-supplied `f28a98c` was the prior baseline. The documentation-only
> commit `2be5212` was the baseline when this plan was written. `0c21200`
> later synchronized the completed P0 work.
>
> Execution status: ExecPlans `0002`, `0003`, `0004`, and `0005` are completed.
> Config, logging, schema, sanitizer presets, benchmark infrastructure, the
> minimum P1 correctness gate, and the first user vertical slice are committed
> locally; push remains a separate user action.

## A. Overall Goal

Complete the missing P0 foundation and prove one P2 business vertical slice
without modifying P1 behavior except for explicitly approved correctness fixes.

The next phase is not "implement all of P2". It is:

1. make runtime configuration, logging, and schema reproducible;
2. establish sanitizer-based safety checks;
3. resolve only P1 correctness defects that block reliable P2 integration;
4. integrate one complete user slice behind the existing HTTP server;
5. defer performance tuning until correctness and integration are stable.

## B. Recommended Phase Order

```text
Baseline documentation correction
    -> P0 completion: config, logging, schema [completed in 0002]
    -> Quality baseline: sanitizers and benchmark [completed in 0003]
    -> P1 minimum correctness gate
    -> P2 user vertical slice
    -> Benchmark and performance baseline
    -> Remaining P2 modules
```

This order differs from the candidate sequence because Sanitizers should exist
before changing frozen P1 code, and the P1 correctness gate should precede the
user/auth integration that depends on HTTP header and connection semantics.

## C. Why Each Direction Belongs Here

### 1. Configuration loading and environment overrides

Completed in `0002`. The prior blocker was that `service/src/main.cpp`
hardcoded `0.0.0.0:8080`; MySQL and Redis primitives could not be constructed
consistently without typed configuration and secret handling.

### 2. Logging initialization

Completed in `0002`. Logging is initialized after configuration and before
infrastructure clients so database, Redis, startup, and shutdown failures can
be diagnosed consistently.

### 3. MySQL schema and initialization

Completed in `0002`. The locked `docs/03-database-design.md` DDL now exists as
canonical scripts under `database/schema/` and is mounted by Compose.

### 4. P2 engine integration into service

Do after config, logging, schema, and the minimum P1 correctness gate.
Integration is a new feature. It should begin with one user vertical slice,
not all five business modules.

Before the user DAO can work, `ConnectionPool` needs explicit execution
metadata such as affected rows, last insert id, and transaction ownership.
The current `Query` API can only return rows.

### 5. P1 HTTP and epoll boundary defects

Do before P2 integration, but as a separately approved freeze exception.
These are correctness defects, not opportunities to add timers, Acceptor,
EventLoopThreadPool, HTTPS, or chunked encoding.

Priority defects and outcomes:

- Completed in `0004`: HTTP header lookup is now case-insensitive.
- Completed in `0004`: duplicate/conflicting `Content-Length` is rejected.
- Completed in `0004`: `Channel::HandleEvent` drains readable data before HUP
  close handling.
- Completed in `0004`: socket writes use `send(MSG_NOSIGNAL)`.
- Retained as technical debt: epoll's active list stores raw `Channel *`;
  callers must preserve channel lifetime through event dispatch.

### 6. Sanitizers

Do before P1 code changes. Sanitizers are build/test infrastructure and do not
change runtime behavior. ASan/UBSan should run first; TSan remains a separate
preset because it is incompatible with ASan.

### 7. Benchmark

Defer until P1 correctness and the first P2 vertical slice are stable.
Benchmarking first would create a baseline against known-defective behavior and
encourage premature optimization.

## D. Dependency Graph

```text
Config
  -> Logging
  -> Service composition
  -> P2 integration

Schema
  -> MySQL integration tests
  -> UserDao
  -> P2 integration

Sanitizer presets
  -> P1 correctness verification
  -> P2 integration verification

P1 HTTP correctness
  -> Auth header handling
  -> P2 user slice

P2 user slice
  -> Benchmark meaningful end-to-end behavior
```

## E. Completed First

The P0 foundation was completed in this order:

1. config loader and environment override precedence;
2. logging initialization and secret redaction;
3. schema/init scripts and non-destructive database verification.

The config loader came first because all later runtime work depends on it.

## F. Defer

- Full resume/job/application/AI business modules.
- RPC, RabbitMQ, crawler, frontend, and final Docker deployment.
- TimerQueue, dedicated Acceptor, graceful signal handling, and
  EventLoopThreadPool as new P1 features.
- HTTP request-size limits, backpressure, and query-string routing hardening
  until the minimum correctness gate is complete.
- Benchmark-based performance tuning.
- Full OSS-Fuzz, clang-tidy/IWYU enforcement, and broad CI expansion.

## G. Defect Fixes vs New Features

### Defect fixes

- case-insensitive HTTP header lookup;
- conflicting `Content-Length` rejection;
- drain readable data before close/error handling;
- SIGPIPE-safe socket writes;
- channel/callback lifetime corrections;
- the dead `ConnectionContext` allocation in `HttpServer::HandleConnection`.

### New features or infrastructure

- configuration and logging;
- schema and initialization scripts;
- connection execution metadata and transactions;
- user DAO/service/controller and auth routes;
- request limits, query parsing, timers, signal handling, and
  EventLoopThreadPool;
- sanitizer presets and benchmarks.

## H. P1 Freeze Impact

Any source change under these paths breaks the current P1 freeze:

- `backend/include/jobpilot/network/`
- `backend/src/network/`
- `backend/include/jobpilot/http/`
- `backend/src/http/`

Only the P1 correctness gate may request a narrow freeze exception. It must be
separately approved, test-first, and limited to demonstrated correctness
problems. Config, logging, schema, sanitizer presets, and P2 wiring do not by
themselves break P1 behavior.

## Level Classification

| Task | Level | ExecPlan |
|---|---|---|
| Fix `project-state.md` actual-HEAD drift | Level 1 | None |
| Configuration loader and env overrides | Level 3 | `0002` completed |
| Logging initialization | Level 2 | Included in completed `0002` |
| MySQL schema and init scripts | Level 3 | `0002` completed |
| Sanitizer build/test matrix | Level 3 | `0003` completed |
| P1 HTTP/epoll correctness gate | Level 3 | `0004` completed |
| P2 user vertical slice | Level 3 | `0005` completed |
| Benchmark and performance baseline | Level 3 | `0003` completed |
| Full P2, RPC, scheduler, or P1 redesign | Not approved now | None |

No Level 4 task is justified by the current candidate list. A Level 4 plan
would require an explicit architecture migration or frozen-contract break.

## Active ExecPlans

None.

## Completed ExecPlans

- `docs/exec-plans/completed/0002-p0-config-logging-schema.md`
- `docs/exec-plans/completed/0003-quality-and-performance-baseline.md`
- `docs/exec-plans/completed/0004-p1-correctness-gate.md`
- `docs/exec-plans/completed/0005-p2-user-vertical-slice.md`
