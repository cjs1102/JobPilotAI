# ExecPlan 0004: P1 Minimum Correctness Gate

> Status: Completed on 2026-10-06.
>
> Archived after regression and sanitizer verification.

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

- [x] Research
- [x] Phase 1: Header and Body Framing Correctness
- [x] Phase 2: Close and Write Correctness
- [x] Phase 3: Channel Lifetime Assessment

### Additional Research Decisions

- Header lookup will scan stored names case-insensitively rather than changing
  the stored map representation.
- Duplicate `Content-Length` will be rejected unconditionally. Rejecting all
  duplicates is the smallest safe behavior for the current HTTP subset; it
  avoids accepting conflicting framing metadata.
- HUP/ERR dispatch will attempt a final read before close handling. Writes are
  not attempted for an error/hangup event.
- Socket writes in `Buffer::WriteFd` and `TcpConnection::SendInLoop` will use
  `send(..., MSG_NOSIGNAL)` rather than changing the process-wide SIGPIPE
  policy.
- The raw `Channel *` active list will remain unchanged unless a deterministic
  test proves a defect. The current server path holds a connection `shared_ptr`
  and defers map removal through `QueueInLoop`.

## Verification Results

- Header case-insensitivity failed 4 focused tests before the fix and passed
  after it.
- Buffer and TcpConnection peer-close writes terminated with SIGPIPE before the
  fix and passed after it.
- The HUP test lost the final readable payload before the fix and passed after
  it.
- Clean `service/` and root builds: passed.
- Normal `service/` and root CTest: 43/43 passed.
- ASan with leak detection: 43/43 passed.
- UBSan with halt-on-error: 43/43 passed.
- TSan concurrency subset: 9/9 passed.
- TSan external-service subset: 6/6 passed.
- TSan exposed a shutdown race in `test_http_server.cpp`; the test now queues
  `Quit()` onto the loop thread before joining. No EventLoop production code
  changed.

## Channel Lifetime Assessment

No reproducible use-after-free was found in the server-managed path:

- `TcpServer` owns connections in `connections_`.
- `TcpConnection::HandleClose` invokes the close callback before map removal.
- `TcpServer` captures a `shared_ptr` in the queued `RemoveConnection` task.
- Therefore a `Channel *` already present in the active event list remains
  backed by a live `TcpConnection` through event dispatch.

The raw pointer remains a fragile API constraint: callers must not destroy a
registered `Channel` while an active event list or callback can still reference
it. This is recorded as technical debt, not changed in 0004.

## Known Issues and Follow-Up

- Query-string routing, request-size limits, and backpressure are not part of
  this gate.
- P1 implementation changes require explicit user approval before execution.
- Raw channel pointers still require strict external lifetime discipline.
