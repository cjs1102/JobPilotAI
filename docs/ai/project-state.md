# JobPilotAI Current Project State

> Snapshot date: 2026-10-06
>
> Branch: `main`
>
> P0 implementation HEAD: `682e26d feat(database): add MySQL initialization schema`
>
> Remote: `origin/main` remains at `2be5212`; the P0 commits are local until a
> separate push action is approved
>
> This document describes the actual files and Git state on disk. It is an AI
> takeover map, not a substitute for the architecture, API, database, or
> execution documents.

## 1. Executive Summary

JobPilotAI has a completed P1 network prototype and a completed P0 runtime
foundation. P2 engine primitives exist but are not yet wired into the service
business layer.

- `service/` is the canonical build entry.
- The root `CMakeLists.txt` is a working unified developer entry.
- Clean Release builds from both entries succeed.
- All 36 current tests pass with MySQL and Redis running and
  `MYSQL_PASSWORD=root`; backend test targets explicitly retain assertions in
  Release builds.
- P1 is complete for the current baseline and frozen. Its implementation has
  not changed since `6236ba1`.
- P2 database and cache engine primitives and tests exist and are committed to
  Git, but they are not wired into the service business layer.
- Typed configuration loading, environment overrides, fail-fast secret
  validation, application logging, and canonical MySQL schema scripts are
  implemented and covered by tests.
- The service startup flow is now
  `Config -> Logging -> EventLoop/HttpServer`; it still does not construct
  MySQL or Redis clients.
- The schema is committed under `database/schema/` and is mounted by Compose
  into the MySQL initialization directory.
- The only untracked local WIP is
  `service/include/jobpilot/service/user/`.

## 2. Git and GitHub State

| Item | Current state |
|---|---|
| Branch | `main` |
| Upstream | `origin/main` |
| Local vs remote | Local has P0 commits; `origin/main` remains `2be5212` |
| Recent history | P0 implementation commits on top of `2be5212` |
| Remote | `https://github.com/cjs1102/JobPilotAI.git` |
| Deleted tracked files | None |
| Staged changes | None |
| Tracked modifications | None |
| Untracked content | `service/include/jobpilot/service/user/` WIP only |
| Push state | P0 changes are committed locally and not pushed |

The current local P0 commits are:

```text
682e26d feat(database): add MySQL initialization schema
2c2f344 feat(logging): initialize application logging
1d3bcbd feat(config): add configuration loading and environment overrides
4b82bd8 docs(plans): define next-phase execution plans
2be5212 docs: update project state after baseline sync
```

The P0 documentation/state commit follows the implementation commits; use
`git rev-parse HEAD` for the exact current documentation commit.

## 3. What Exists on Disk

### Backend

```text
backend/
├── include/jobpilot/
│   ├── cache/
│   ├── core/          # Config, Logger, ThreadPool interfaces
│   ├── database/
│   ├── http/
│   ├── log/          # empty directory
│   └── network/
├── src/
│   ├── cache/
│   ├── core/          # Config, Logger, ThreadPool implementations
│   ├── database/
│   ├── http/
│   ├── log/          # empty directory
│   └── network/
└── tests/             # P1 network/HTTP plus Config, Logger, Schema tests
```

### Service

```text
service/
├── CMakeLists.txt
├── conanfile.txt
├── config/config.yaml
├── include/jobpilot/
│   ├── error.h
│   └── service/user/     # local untracked WIP: interfaces and DAO declaration
├── src/
│   ├── main.cpp
│   ├── router.cpp
│   └── user/             # empty directory
├── tests/test_basic.cpp
└── build/                # generated, ignored
```

### Other Project Areas

- `third_party/libbcrypt/`: vendored bcrypt implementation.
- `database/schema/`: canonical, idempotent MySQL initialization scripts.
- `docker/mysql/init/`: legacy empty directory; no longer mounted.
- `docs/`: architecture, API, database, execution, ADR, AI, and ExecPlan docs.
- `gateway/`, `ai_service/`, `crawler/`, and `scripts/`: absent.
- `.agents/skills/jobpilot-reactor-review/`: project-specific AI workflow.

## 4. Current Architecture

### Runtime Entry

`service/src/main.cpp` currently:

1. Parses the optional `--config=<path>` argument.
2. Loads `Config` from YAML plus environment overrides.
3. Initializes the application logger from `log.level`.
4. Logs non-secret MySQL, Redis, and service configuration.
5. Creates an `EventLoop` and `HttpServer` on the configured host and port.
6. Registers routes from `service/src/router.cpp`.
7. Starts the server and blocks in `EventLoop::Loop()`.

The service still does not construct MySQL or Redis clients and does not install
signal handlers. Those remain explicit later-stage work.

### Network and HTTP Call Graph

```text
main
  -> EventLoop
       -> Poller
            -> EpollPoller
       -> Channel
  -> HttpServer
       -> TcpServer
            -> acceptChannel
            -> TcpConnection map
       -> HttpRouter
       -> ThreadPool
       -> per-connection HTTP parser/context

epoll event
  -> EventLoop::Loop
  -> Channel::HandleEvent
  -> TcpConnection::HandleRead or HandleWrite
  -> HttpServer::HandleMessage
  -> ThreadPool worker
  -> HttpRouter
  -> EventLoop::RunInLoop
  -> TcpConnection::Send
```

### Class Responsibilities

| Class | Actual responsibility |
|---|---|
| `EventLoop` | Owns the poller, wakeup eventfd, pending functors, thread identity, and event dispatch loop |
| `Poller` | Abstract event-registration interface |
| `EpollPoller` | epoll fd, registration set, event buffer, and epoll_ctl operations |
| `Channel` | Combines an fd with events and read/write/close callbacks |
| `Buffer` | Input/output byte buffer with readv and write support |
| `TcpConnection` | Owns one socket fd, channel, input/output buffers, state, and callbacks |
| `TcpServer` | Owns the listening socket, accept channel, and connection map |
| `HttpRequestParser` | Incremental HTTP/1.1 GET/POST parser for Content-Length bodies |
| `HttpResponse` | Response builder and serializer |
| `HttpRouter` | Static and `{parameter}` route matching |
| `HttpServer` | Connects TcpServer, parser, router, thread pool, and connection contexts |
| `ThreadPool` | Fixed worker queue with graceful drain on destruction |
| `ConnectionPool` | MySQL connection pool with prepared-query execution |
| `Result` | Column names, rows, nullable values, and bounds-checked access |
| `RedisClient` | Redis connection pool and GET/SET/SETEX/DEL/INCRBY operations |
| `SessionManager` | `session:{token}` mapping to user id with TTL |

### Thread Model

- `EventLoop::Loop()` runs on the thread that creates the loop.
- Network callbacks run on the event-loop thread.
- `HttpServer` owns a fixed four-thread `ThreadPool`.
- Request handlers run on worker threads.
- Worker results return through `EventLoop::RunInLoop`.
- Per-connection HTTP response ordering is reconstructed by sequence numbers.
- Database and Redis code is thread-safe at the pool level but is not connected
  to the HTTP service yet.

### Resource Lifecycle

- `EpollPoller` owns and closes its epoll fd.
- `EventLoop` owns and closes its eventfd.
- `TcpServer` owns and closes its listening fd.
- `TcpConnection` owns and closes its socket fd.
- `Channel` holds a non-owning fd and raw callback state.
- epoll stores raw `Channel *` pointers in `epoll_event.data.ptr`.

### Important Lifecycle Invariant

The current model is safe only when a `TcpConnection` remains alive while its
`Channel` is registered and while its callbacks execute. The design does not
provide an independent shared lifetime for `Channel`; this is a future hardening
area.

## 5. P1 Assessment

Status: P1 is complete for the current baseline and frozen. Its network and HTTP
implementation has not changed since `6236ba1`. The items below remain known
follow-up hardening work, not pending P1 implementation.

### Implemented

- `EventLoop` with eventfd wakeup and cross-thread `RunInLoop`.
- `Poller` and `EpollPoller`.
- `Channel` callback dispatch.
- `Buffer` append, retrieve, readv, write, expansion, and space reuse.
- `TcpConnection` read, write, partial write, output buffering, and shutdown
  entry.
- `TcpServer` non-blocking accept loop and connection map.
- HTTP/1.1 subset for GET and POST.
- `Content-Length` request body framing.
- Keep-alive and pipelined response ordering.
- Static and parameterized route matching.
- Fixed thread pool request dispatch.
- Integration tests for health, POST, 404, invalid input, keep-alive,
  pipelining, half-packet input, and connection close.

### Not Implemented or Not Proven

- Dedicated `TimerQueue` or timer API.
- Separate `Acceptor` class.
- `EventLoopThreadPool` or one-loop-per-thread server model.
- Graceful signal handling and service shutdown.
- Request/header/body size limits.
- Case-insensitive HTTP header handling.
- Full HTTP error semantics and malformed-input hardening.
- `EPOLLHUP` plus readable-data handling.
- Protection from `SIGPIPE` through `send(MSG_NOSIGNAL)` or process policy.
- Sanitizer build matrix.
- Benchmark baseline and `wrk` QPS evidence.
- Backpressure, high-water marks, or idle connection timeout policy.

## 6. P2 Assessment

Status: the engine primitives are implemented, tested, and committed to Git.
They are not yet connected to the service business layer.

### Implemented as Engine Primitives

- MySQL connection pool.
- Prepared statement binding for integer, unsigned integer, double, string,
  and NULL parameters.
- SQL result representation with null and empty-string distinction.
- Redis connection pool.
- Redis GET, SET, SETEX, DEL, and INCRBY.
- Redis session manager.
- bcrypt hashing and verification.
- Integration tests for MySQL, Redis, session expiration, concurrency, and
  bcrypt.

### Not Integrated

- No user DAO implementation.
- No MySQL or Redis construction in `main`.
- No HTTP user registration or login routes.
- No transaction abstraction.
- No DAO/service/controller business flow.

Configuration, logging, and schema are P0 infrastructure and are now present;
they are not P2 business integration.

## 7. Build and Test State

### Supported Build Entries

`service/` is the canonical build and release entry:

```bash
cd service
conan install . -of build --build=missing
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The root project is a supported unified developer entry when it receives the
Conan-generated toolchain:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/service/build/conan_toolchain.cmake"
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

### Verified Results

| Check | Result |
|---|---|
| Clean `service/` configure and Release build | Passed |
| Clean root configure and Release build | Passed |
| `service/` CTest | 36/36 passed with `MYSQL_PASSWORD=root` |
| Root CTest | 36/36 passed with `MYSQL_PASSWORD=root` |
| ASan/UBSan targeted P0/boundary tests | 18/18 passed |
| Missing `MYSQL_PASSWORD` startup | Failed fast with exit code 1 and a critical log |
| Valid startup | Loaded YAML, initialized logging, and started HTTP on configured host/port |
| Fresh isolated schema initialization | Passed on a non-default database name; 11 tables created |
| Test compile flags | `-DNDEBUG` followed by `-UNDEBUG` |
| C++ language mode | `-std=c++17` |

The full CTest suites require live MySQL and Redis. `SchemaTest` also requires
the canonical schema to be applied to the configured database.

### CMake Resolution

`CMakeLists.txt` adds `backend` and `service`; `service/CMakeLists.txt` adds
`../backend backend` only when `PROJECT_IS_TOP_LEVEL`. Root adds backend once,
while a standalone service build still adds it for itself.

### Test Reliability

Backend test targets receive `-UNDEBUG` after the Release `-DNDEBUG` flag, so
their existing C assertions execute during normal CTest runs. The repository
can still migrate individual tests to GoogleTest assertions later, but that is
no longer required for baseline safety.

## 8. Docker and Infrastructure State

- MySQL container: running and healthy.
- Redis container: running and healthy.
- MySQL database `jobpilot`: exists.
- MySQL tables: the 11 canonical tables are present.
- `database/schema/`: mounted at `/docker-entrypoint-initdb.d:ro`; scripts run
  only when the MySQL data volume is initialized for the first time.
- `docker/mysql/init/`: legacy empty directory and no longer mounted.
- No backend container or Dockerfile exists yet.
- Compose reads MySQL credentials from required environment variables.
- `.env.example` contains placeholder values only.
- `docker compose --env-file .env.example config` validates successfully.
- Reusing an existing MySQL volume after changing credentials requires the
  relevant volume to be recreated or the password to be rotated in the server.

## 9. Current Local WIP and Ignored State

### Untracked WIP

- `service/include/jobpilot/service/user/user_dao.h`
- `service/include/jobpilot/service/user/user_dao_interface.h`

These files define P2 user DAO interfaces and declarations only. They have no
implementation or tests and remain intentionally untracked.

### Committed Baseline

The following are no longer local-only changes:

- P2 database and cache headers, sources, and tests.
- The vendored bcrypt source and provenance note.
- Project documentation and AI infrastructure.
- Root/service CMake repairs and assertion-preserving test configuration.
- Config loader and environment override implementation with tests.
- spdlog initialization wrapper, startup logging, and logger tests.
- Canonical `database/schema/` scripts, schema integration tests, and Compose
  initialization mount.
- Safe local configuration templates and the expanded `.gitignore`.

They are included in the local P0 commits listed in Section 2; the older
six-commit baseline remains in history under `2be5212`.

### Ignored Local State

- `service/build/`
- `service/CMakeUserPresets.json`
- Local `.env`, local config variants, logs, caches, binaries, coverage,
  profiler output, core dumps, and IDE state.

## 10. Sensitive Information Review

- No real API key, token, private key, or production credential was found.
- `.env` is absent.
- `.env.example` contains development placeholders only.
- `service/config/config.yaml` contains no password.
- `docker-compose.yml` contains credential variable references, not secrets.
- Tests contain a sample bcrypt password, not a real credential.
- `.gitignore` has been expanded to ignore `.env.*` while preserving
  `.env.example`, local config variants, secrets, and generated artifacts.

Compose configuration now follows the template plus environment-variable
pattern.

## 11. Third-Party Review

`third_party/libbcrypt/` is a real build dependency, not generated output.

- `bcrypt.h` identifies Ricardo Garcia's wrapper library and CC0/public-domain
  dedication.
- `crypt_blowfish.c` identifies Solar Designer and Openwall, with public-domain
  dedication and a permissive fallback statement.
- License text is embedded in the source headers.
- `PROVENANCE.md` records the wrapper and crypt_blowfish origins.

Recommendation: commit the vendored source because the current CMake build uses
it directly. Keep the provenance note with the source.

## 12. Technical Debt and Risks

| Priority | Issue | Effect |
|---|---|---|
| High | HTTP/header limits and case-insensitive headers are absent | Parsing correctness and DoS risk |
| High | `EPOLLERR`/`EPOLLHUP` returns before readable data | Peer-close data loss possible |
| High | Raw `Channel *` in epoll active list | Lifetime risk under reentrant removal |
| Medium | `write` may trigger `SIGPIPE` | Process termination risk |
| Medium | HTTP contexts use a discarded temporary allocation | Small bug and unnecessary allocation |
| Medium | No graceful signal handling | Service cannot shut down cleanly |
| Medium | No sanitizer or benchmark configuration | Memory, race, and performance risks unmeasured |
| Medium | User DAO has declarations but no implementation | P2 skeleton only |
| Medium | P2 engine primitives are not wired into service | No business flow yet |
| Medium | MySQL and Redis clients are not constructed by the service | Runtime infrastructure is not connected yet |
| Medium | Redis/MySQL tests require live services | Clean CTest is not self-contained |
| Low | Schema scripts require a fresh volume to execute automatically | Existing volumes need explicit migration/application |
| Low | Existing MySQL volumes retain old credentials | Credential changes need volume recreation |

## 13. Git State Classification

### Committed Baseline

The P0 implementation is committed locally in the logical commits listed in
Section 2. `origin/main` has not been updated in this phase. No tracked
modifications or staged changes should remain after the final state-document
commit.

### Untracked WIP

- `service/include/jobpilot/service/user/user_dao.h`
- `service/include/jobpilot/service/user/user_dao_interface.h`

Keep these files untracked until the P2 user module has an implementation plan,
implementation, and tests.

### Ignored

- `service/build/`.
- `service/CMakeUserPresets.json`.
- `.env`, local config variants, caches, logs, binaries, coverage, profiler
  output, core dumps, and IDE state.

## 14. Next Development Entry

The local P0 baseline is the next development starting point. P1 remains frozen
and the only untracked local content is the user DAO WIP.

The next implementation sequence is:

1. Execute ExecPlan `0003` for sanitizer and performance-baseline
   infrastructure.
2. Execute ExecPlan `0004` for the minimum, separately approved P1 correctness
   gate before changing frozen network behavior.
3. Execute ExecPlan `0005` for the first P2 user vertical slice.
4. Wire MySQL, Redis, and Session only during P2 integration.
5. Keep `service/include/jobpilot/service/user/` untracked until the user module
   is implemented and committed as its own logical change.
