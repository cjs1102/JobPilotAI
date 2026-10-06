# ExecPlan 0004: P1 Minimum Correctness Gate

## Background

P1 is complete and frozen. The freeze preserves behavior; it does not make
known correctness defects production-safe.

The minimum gate targets defects that can affect P2 integration:

- case-sensitive HTTP header lookup;
- conflicting or duplicate `Content-Length`;
- close/error handling that can skip readable data;
- SIGPIPE risk on socket write;
- raw channel pointers in the epoll active list.

## Goal

Fix only demonstrated P1 correctness defects, with regression tests and
sanitizer verification, while preserving the existing public interfaces where
possible.

## Non-Goals

- No TimerQueue.
- No dedicated Acceptor.
- No EventLoopThreadPool.
- No HTTPS, WebSocket, or chunked transfer encoding.
- No general HTTP feature expansion.

## Constraints and Invariants

- P1 freeze requires an explicit exception for every production source change.
- Tests must reproduce each defect before the fix.
- Existing HTTP behavior and route registration must remain compatible.
- Resource ownership, fd closure, callback lifetime, and event-loop thread
  affinity must remain explicit.

## Research Evidence

- `HttpRequest::HasHeader` and `GetHeader` use case-sensitive map lookup.
- `HttpRequestParser::ParseHeaders` queries exact `"Content-Length"` and
  `"Connection"`.
- `Channel::HandleEvent` returns immediately for `EPOLLERR/EPOLLHUP`.
- `TcpConnection::SendInLoop` and `Buffer::WriteFd` use `write`.
- `Poller::ChannelList` stores raw `Channel *`.
- `HttpServer::HandleConnection` creates and discards an unused allocation.

## Selected Design

1. Normalize header names for lookup while preserving original header storage
   if needed.
2. Reject duplicate or conflicting `Content-Length` according to the locked
   HTTP subset.
3. On read/error events, drain readable data before close handling where
   possible.
4. Use a SIGPIPE-safe send path.
5. Reproduce channel lifetime risk before changing ownership; prefer the
   smallest safe protocol change rather than a network-stack rewrite.
6. Remove the unused HTTP context allocation as a small cleanup.

## Rejected Alternatives

- Redesigning the whole network stack.
- Adding timers or one-loop-per-thread while touching P1.
- Ignoring header case because current tests use canonical capitalization.
- Process-wide SIGPIPE ignore without documenting or testing the policy.

## Implementation Phases

### Phase 1: Header and Body Framing Correctness

- add failing tests for lowercase/mixed-case headers;
- add duplicate/conflicting `Content-Length` tests;
- implement the narrow parser/request fix.

### Phase 2: Close and Write Correctness

- add a final-bytes-before-HUP test;
- add a peer-close write test;
- implement the narrow channel/write fixes.

### Phase 3: Channel Lifetime

- construct a deterministic test for removal during active event dispatch;
- document the exact failure mechanism;
- implement only if the test proves the defect.

## Verification and Rollback

Verification:

- focused unit tests;
- full P1 CTest;
- ASan/UBSan and TSan where applicable;
- normal HTTP integration tests.

Rollback:

- each defect fix is a separate commit;
- any lifecycle change must have an isolated rollback.

## Current Progress

Not started.

## Known Issues and Follow-Up

- Query-string routing, request-size limits, and backpressure are not part of
  this gate.
- P1 implementation changes require explicit user approval before execution.
