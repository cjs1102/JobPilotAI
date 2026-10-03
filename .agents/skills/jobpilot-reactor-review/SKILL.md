---
name: jobpilot-reactor-review
description: Review JobPilotAI Reactor network changes involving EventLoop, Poller, epoll, fd or callback lifetime, thread ownership, shutdown, concurrency, and network tests. Use before finalizing any change in the JobPilotAI network subsystem.
---

# Jobpilot Reactor Review

## Use When

- A change touches `EventLoop`, `Poller`, `EpollPoller`, `Channel`, `Buffer`,
  `TcpConnection`, `TcpServer`, timers, fd ownership, callbacks, or thread
  handoff.
- A network bug involves hangs, missed events, use-after-close, partial writes,
  shutdown, or race conditions.
- A network change needs a final review, exploitability check, or test-gap
  audit.

## Do Not Use

- Do not use for unrelated business, database, cache, HTTP routing, or frontend
  changes.
- Do not use as a substitute for implementing and testing the requested fix.
- Do not use to justify a network-layer rewrite when a narrow correction is
  sufficient.

## Read First

1. `AGENTS.md`.
2. The network and testing sections of `docs/06-coding-standard.md`.
3. The network boundaries in `docs/02-system-architecture.md`.
4. The task execution and stop lines in `docs/08-execution-guide.md`.
5. The changed files and the relevant existing tests under `backend/tests/`.

## Review Checklist

### Event Loop and Thread Ownership

- Confirm that each channel has one event-loop owner.
- Check `RunInLoop` and `QueueInLoop` semantics, wakeup behavior, and pending
  functor execution.
- Verify that cross-thread work is queued rather than directly touching fd or
  channel state.
- Check behavior during quit, shutdown, and callback re-entry.

### Poller and epoll

- Confirm update and removal symmetry for every registration.
- Check `EPOLLIN`, `EPOLLOUT`, `EPOLLERR`, and `EPOLLHUP` handling.
- Make LT or ET behavior explicit and verify `EAGAIN` handling.
- Check `epoll_ctl`, `epoll_wait`, `EINTR`, and wakeup error paths.

### fd and Connection Lifetime

- Verify non-blocking setup, close-on-exec where appropriate, and exactly-once
  close.
- Check failure cleanup after every registration or connection step.
- Prevent use-after-close, dangling callbacks, and destruction during callback
  dispatch.
- Confirm read, write, shutdown, and half-close semantics.

### Concurrency and Errors

- Check mutex ownership, lock ordering, atomics, and condition-variable use.
- Ensure callbacks do not outlive their connection, channel, or event loop.
- Check partial writes, backpressure, buffer growth, and idle or timeout paths.
- Verify errors propagate without leaking or terminating the loop.

### Tests

- Require focused tests for the changed invariant.
- Prefer deterministic event-loop tests over sleep-based timing assumptions.
- Cover failure and shutdown paths, not only the happy path.

## Required Output

- Findings first, ordered by severity and grounded in file and line references.
- Invariants checked and their observed status.
- Tests run and results.
- Verification gaps and residual risk.

## Verification

- Run the focused network tests and the relevant full test subset.
- Use ASan and UBSan for lifetime or bounds changes.
- Use TSan for cross-thread changes when the build configuration supports it.
- Do not claim thread safety or leak freedom without a corresponding check or
  an explicit limitation.
