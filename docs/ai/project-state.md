# JobPilotAI Current Project State

> Snapshot date: 2026-10-04
>
> Branch: `main`
>
> HEAD: `6236ba1 feat: complete P1 HTTP server and thread pool`
>
> Remote: `origin/main` at the same commit
>
> This document describes the actual files on disk, including files that are
> not tracked by Git yet. It is an AI takeover map, not a substitute for the
> architecture, API, database, or execution documents.

## 1. Executive Summary

JobPilotAI is between a completed P1 network prototype and an incomplete P0/P2
engineering foundation.

- `service/` is the canonical build entry.
- The root `CMakeLists.txt` is a working unified developer entry.
- Clean Release builds from both entries succeed.
- All 20 current tests pass with MySQL and Redis running and
  `MYSQL_PASSWORD=root`; backend test targets explicitly retain assertions in
  Release builds.
- P1 has a working epoll/Reactor/HTTP subset, but timers, sanitizer evidence,
  graceful shutdown, performance evidence, and several network edge cases are
  still missing.
- P2 has MySQL, Redis, session, SQL-result, and bcrypt primitives, but they are
  not wired into the service and no database schema is installed.
- Most current work exists only on disk and is not committed to GitHub.

## 2. Git and GitHub State

| Item | Current state |
|---|---|
| Branch | `main` |
| Upstream | `origin/main` |
| Local vs remote | Same commit before current uncommitted work |
| Recent history | 5 commits, latest `6236ba1` |
| Remote | `https://github.com/cjs1102/JobPilotAI.git` |
| Deleted tracked files | None |
| Staged changes | None observed |
| Local modifications | P1/P2/build/config changes plus baseline hardening changes from this audit |
| Untracked content | P2 engine code, tests, docs, AI infrastructure, vendored bcrypt |

No commit or push was performed during this audit.

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
│   └── service/user/     # interfaces and DAO declaration only
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

## 9. Local Changes Not Yet in Git

### Formal P2 Engine Code

- `backend/include/jobpilot/database/`
- `backend/src/database/`
- `backend/include/jobpilot/cache/`
- `backend/src/cache/`

### P2 Tests

- `backend/tests/test_mysql_pool.cpp`
- `backend/tests/test_sql_result.cpp`
- `backend/tests/test_redis_client.cpp`
- `backend/tests/test_bcrypt.cpp`

### Project Configuration and Build Changes

- `backend/CMakeLists.txt`
- `CMakeLists.txt`
- `service/CMakeLists.txt`
- `service/config/config.yaml`
- `backend/tests/test_tcp_connection_write_buffer.cpp`
- `backend/tests/test_redis_client.cpp`
- `docker-compose.yml`
- `.env.example`

### Third-Party Code

- `third_party/libbcrypt/`
- `third_party/libbcrypt/PROVENANCE.md`

### AI Infrastructure

- `AGENTS.md`
- `.agents/skills/jobpilot-reactor-review/`
- `docs/ai/`
- `docs/exec-plans/`

### Documentation

- All files under `docs/` are currently untracked.
- `docs/ai/github_resources.md` must be preserved.

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

## 13. Git Classification

### Should Commit

- All project documentation under `docs/`.
- `AGENTS.md` and `.agents/skills/jobpilot-reactor-review/`.
- Database and cache engine headers/sources.
- Their tests.
- `third_party/libbcrypt/`.
- `third_party/libbcrypt/PROVENANCE.md`.
- The `.gitignore` update.
- The repaired root/service CMake configuration.
- The assertion-preserving test configuration.
- Docker Compose environment substitution and `.env.example`.

### Should Be Ignored

- `service/build/`.
- `service/CMakeUserPresets.json`.
- Future `.env` and local config variants.
- Compiled libraries, object files, dependency files, logs, core dumps,
  coverage, profiler output, and IDE state.

### Keep Local, Do Not Commit

- Machine-specific Conan/CMake generated files.
- Local `.env` or `config.local.yaml`.
- Local build directories.

### Needs Human Confirmation

- Whether to commit `service/include/jobpilot/service/user/` now as a P2
  skeleton or wait until a `UserDao` implementation exists.
- Whether the vendored bcrypt dependency should be committed as one dedicated
  commit before the P2 engine changes.

## 14. Next Development Entry

The build and test baseline is ready for review. The next implementation
sequence is:

1. Add configuration loading and logging.
2. Add database schema and connect MySQL/Redis only when P2 starts.
3. Harden P1 HTTP parsing and event-close handling.
4. Add sanitizer presets and one benchmark baseline.
5. Keep P1 network behavior frozen unless a verified defect requires a change.
