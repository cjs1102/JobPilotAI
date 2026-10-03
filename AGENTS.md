# JobPilotAI Project Guide

## Project

JobPilotAI is a C++17 Linux backend for an AI-assisted job application
platform. The current focus is the self-developed Reactor network library and
the HTTP server foundation. Business correctness still depends on the existing
architecture, database, API, and execution documents.

## Read First

1. `docs/08-execution-guide.md` for the authoritative P0-P5 execution order and
   stop lines.
2. `docs/02-system-architecture.md` for module and dependency boundaries.
3. `docs/06-coding-standard.md` for local naming, layering, logging, testing,
   and Git rules.
4. `docs/ai/project-state.md` for the current disk, Git, build, and test state.
5. `docs/ai/README.md` for the project AI knowledge map.
6. `docs/ai/github_resources.md` for reference material. Do not copy third-party
   implementation code.
7. `.agents/skills/jobpilot-reactor-review/SKILL.md` when working on the network
   subsystem.

## Current Stage

- Phase: P1 network core.
- Core area: `backend/include/jobpilot/network/` and `backend/src/network/`.
- Existing network building blocks: `EventLoop`, `Poller`, `EpollPoller`,
  `Channel`, `Buffer`, `TcpConnection`, and `TcpServer`.
- Next direction: harden Poller and epoll behavior, timer support, connection
  lifecycle, thread handoff, and network tests.

## Key Directories

| Path | Responsibility |
|---|---|
| `backend/include/jobpilot/` | Public core interfaces |
| `backend/src/` | Core implementation |
| `backend/tests/` | GoogleTest unit and behavior tests |
| `service/` | Runnable service and Conan entry point |
| `docs/` | Architecture, contracts, execution guide, ADRs |
| `docs/ai/` | AI workflow and reference documentation |
| `third_party/` | Vendored third-party code only when explicitly required |

## Build and Verification

- Build system: CMake 3.22+ and Conan.
- Language: C++17 on Linux.
- Do not attempt to compile or run the epoll network layer natively on Windows.
- Use the exact environment and dependency sequence in
  `docs/08-execution-guide.md`.
- Prefer `cmake --build build` and
  `ctest --test-dir build --output-on-failure` when a configured build tree
  exists.
- Do not invent Conan or CMake flags. Inspect `service/conanfile.txt`,
  `service/CMakeLists.txt`, and the execution guide first.
- A change is not complete until the relevant tests are run or an explicit
  verification gap is reported.

## Hard Constraints

- Do not modify database schema or API contracts without updating the relevant
  documents and obtaining review first.
- Keep business logic out of the network layer.
- Preserve the Controller -> Service -> DAO dependency direction.
- Use RAII and explicit ownership for sockets, file descriptors, channels,
  connections, and locks.
- Do not leave `std::cout`, `printf`, raw `new/delete`, or raw owning pointers
  in production code.
- Use structured JSON, parameterized SQL, and typed error codes as defined by
  the project documents.
- Do not silently weaken warnings, tests, Sanitizers, or error handling to make
  a change pass.
- Do not revert unrelated local changes.

## Network Invariants

- A channel belongs to one event-loop thread at a time.
- Every fd registration must have a matching removal path on failure and
  destruction.
- Callbacks must not outlive their connections, channels, or event loops.
- `RunInLoop` and `QueueInLoop` must preserve event-loop thread ownership.
- Shutdown must be idempotent and must not use a closed descriptor.
- LT/ET behavior and `EAGAIN` handling must be explicit and tested.
- Treat connection lifetime and callback lifetime as one design problem.

## AI Workflow

- Follow the task-level workflow defined in the global `~/.codex/AGENTS.md`.
- For Level 3 and Level 4 work, create an ExecPlan under
  `docs/exec-plans/active/` before implementation.
- Use `jobpilot-reactor-review` for network subsystem changes.
- Use the global `cpp-code-review` and `cpp-build-diagnose` Skills instead of
  duplicating those workflows here.
