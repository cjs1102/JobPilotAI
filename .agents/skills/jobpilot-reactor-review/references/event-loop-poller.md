# Event Loop and Poller Checklist

## Event Loop and Thread Ownership

- Confirm that each channel has one event-loop owner.
- Check `RunInLoop` and `QueueInLoop` semantics, wakeup behavior, and pending
  functor execution.
- Verify that cross-thread work is queued rather than directly touching fd or
  channel state.
- Check behavior during quit, shutdown, and callback re-entry.

## Poller and epoll

- Confirm update and removal symmetry for every registration.
- Check `EPOLLIN`, `EPOLLOUT`, `EPOLLERR`, and `EPOLLHUP` handling.
- Make LT or ET behavior explicit and verify `EAGAIN` handling.
- Check `epoll_ctl`, `epoll_wait`, `EINTR`, and wakeup error paths.
