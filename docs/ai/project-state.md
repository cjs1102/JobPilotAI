# JobPilotAI Current Project State

> Snapshot date: 2026-10-06
>
> Branch: `main`
>
> HEAD: `f28a98c chore(config): add safe local configuration template`
>
> Remote: `origin/main` at the same commit; local and remote are fully synced
>
> This document describes the actual files and Git state on disk. It is an AI
> takeover map, not a substitute for the architecture, API, database, or
> execution documents.

## 1. Executive Summary

JobPilotAI is between a completed P1 network prototype and an incomplete P0/P2
engineering foundation.

- `service/` is the canonical build entry.
- The root `CMakeLists.txt` is a working unified developer entry.
- Clean Release builds from both entries succeed.
- All 20 current tests pass with MySQL and Redis running and
  `MYSQL_PASSWORD=root`; backend test targets explicitly retain assertions in
  Release builds.
- P1 is complete for the current baseline and frozen. Its implementation has
  not changed since `6236ba1`.
- P2 database and cache engine primitives and tests exist and are committed to
  Git, but they are not wired into the service business layer.
- No database schema is installed yet.
- Six baseline commits are pushed to `origin/main`.
- The only untracked local WIP is
  `service/include/jobpilot/service/user/`.

## 2. Git and GitHub State

| Item | Current state |
|---|---|
| Branch | `main` |
| Upstream | `origin/main` |
| Local vs remote | Identical at `f28a98c` |
| Recent history | Six baseline commits on top of `6236ba1` |
| Remote | `https://github.com/cjs1102/JobPilotAI.git` |
| Deleted tracked files | None |
| Staged changes | None |
| Tracked modifications | None |
| Untracked content | `service/include/jobpilot/service/user/` WIP only |
| Push state | `main == origin/main` at `f28a98c` |

The six baseline commits are:

```text
f28a98c chore(config): add safe local configuration template
71a3a9b build: support root and service CMake entry points
ffd93d3 test: preserve assertions and stabilize timing tests
4edf004 feat(engine): add database and cache primitives
362f504 build(third_party): vendor libbcrypt with provenance
9fae43a docs(ai): establish project takeover and AI infrastructure
```

## 3. What Exists on Disk

### Backend

```text
backend/
├── include/jobpilot/
│   ├── cache/
│   ├── core/
│   ├── database/
│   ├── http/
│   ├── log/          # empty directory
│   └── network/
├── src/
│   ├── cache/
│   ├── core/
│   ├── database/
│   ├── http/
│   ├── log/          # empty directory
│   └── network/
└── tests/
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
- `docker/mysql/init/`: empty directory.
- `docs/`: architecture, API, database, execution, ADR, AI, and ExecPlan docs.
- `database/`, `gateway/`, `ai_service/`, `crawler/`, and `scripts/`: absent.
- `.agents/skills/jobpilot-reactor-review/`: project-specific AI workflow.

## 4. Current Architecture

### Runtime Entry

`service/src/main.cpp` currently:

1. Creates an `EventLoop`.
2. Creates an `HttpServer` on `0.0.0.0:8080`.
3. Registers routes from `service/src/router.cpp`.
4. Starts the server.
5. Blocks in `EventLoop::Loop()`.

The port is hardcoded. The service does not load `config/config.yaml`, initialize
MySQL or Redis, configure logging, or install signal handlers.

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

- No config loader or environment override implementation.
- No service logging initialization.
- No user DAO implementation.
- No database schema files.
- No MySQL or Redis construction in `main`.
- No HTTP user registration or login routes.
- No transaction abstraction.
- No DAO/service/controller business flow.

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
| `service/` CTest | 20/20 passed with `MYSQL_PASSWORD=root` |
| Root CTest | 20/20 passed with `MYSQL_PASSWORD=root` |
| Test compile flags | `-DNDEBUG` followed by `-UNDEBUG` |
| C++ language mode | `-std=c++17` |

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
- MySQL tables: none observed.
- `docker/mysql/init/`: empty, so no schema or seed is installed at container
  initialization.
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
- Safe local configuration templates and the expanded `.gitignore`.

They are included in the six baseline commits listed in Section 2.

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
| High | P0 config and logging are not implemented | `main` hardcodes runtime settings |
| High | No schema files or MySQL tables | P2 cannot persist business data |
| High | HTTP/header limits and case-insensitive headers are absent | Parsing correctness and DoS risk |
| High | `EPOLLERR`/`EPOLLHUP` returns before readable data | Peer-close data loss possible |
| High | Raw `Channel *` in epoll active list | Lifetime risk under reentrant removal |
| Medium | `write` may trigger `SIGPIPE` | Process termination risk |
| Medium | HTTP contexts use a discarded temporary allocation | Small bug and unnecessary allocation |
| Medium | No graceful signal handling | Service cannot shut down cleanly |
| Medium | No sanitizer or benchmark configuration | Memory, race, and performance risks unmeasured |
| Medium | User DAO has declarations but no implementation | P2 skeleton only |
| Medium | P2 engine primitives are not wired into service | No business flow yet |
| Medium | Redis/MySQL tests require live services | Clean CTest is not self-contained |
| Low | Existing MySQL volumes retain old credentials | Credential changes need volume recreation |

## 13. Git State Classification

### Committed Baseline

The baseline is committed and pushed in six logical commits listed in Section
2. No tracked modifications or staged changes remain.

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

The committed and pushed baseline is the next development starting point. P1
remains frozen and the only untracked local content is the user DAO WIP.

The next implementation sequence is:

1. Implement configuration loading and logging without changing P1 behavior.
2. Add database schema and initialization files.
3. Wire MySQL, Redis, and Session only when P2 service integration begins.
4. Address P1 hardening only through separately planned, test-backed changes.
5. Add sanitizer presets and a benchmark baseline.
6. Keep `service/include/jobpilot/service/user/` untracked until the user module
   is implemented and committed as its own logical change.
