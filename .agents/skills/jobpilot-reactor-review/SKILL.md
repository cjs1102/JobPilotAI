---
name: jobpilot-reactor-review
description: Review JobPilotAI Reactor network changes involving EventLoop, Poller, epoll, fd or callback lifetime, thread ownership, shutdown, concurrency, and network tests. Use before finalizing any change in the JobPilotAI network subsystem.
---

# JobPilotAI Reactor Review

## Use

Use for changes touching `EventLoop`, `Poller`, `EpollPoller`, `Channel`,
`Buffer`, `TcpConnection`, `TcpServer`, fd/callback lifetime, thread handoff,
shutdown, or network tests.

Do not use for unrelated business, database, cache, routing, or frontend work,
and do not use it to justify a network-layer rewrite.

## Core Rules

- A channel has one event-loop owner at a time.
- Registration and removal must be symmetric on success, failure, and
  destruction.
- Callbacks must not outlive their connection, channel, or event loop.
- Cross-thread work goes through `RunInLoop`/`QueueInLoop`.
- LT/ET behavior and `EAGAIN` handling must be explicit and tested.
- Prefer deterministic event-loop tests over sleep-based timing.

## Reference Routing

- EventLoop, Poller, epoll, wakeup, or dispatch:
  [references/event-loop-poller.md](references/event-loop-poller.md)
- Channel, fd, TcpConnection, shutdown, or callback lifetime:
  [references/connection-lifetime.md](references/connection-lifetime.md)
- Network tests, sanitizers, or verification:
  [references/network-tests.md](references/network-tests.md)

Read only the reference matching the current change.

## Output

Findings first with file/line references, then invariants checked, tests run,
and residual risk. Do not claim thread safety or leak freedom without a
matching check or explicit limitation.
