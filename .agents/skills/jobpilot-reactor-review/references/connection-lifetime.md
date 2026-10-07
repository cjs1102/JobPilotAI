# Connection and Callback Lifetime Checklist

## fd and Connection Lifetime

- Verify non-blocking setup, close-on-exec where appropriate, and exactly-once
  close.
- Check failure cleanup after every registration or connection step.
- Prevent use-after-close, dangling callbacks, and destruction during callback
  dispatch.
- Confirm read, write, shutdown, and half-close semantics.

## Concurrency and Errors

- Check mutex ownership, lock ordering, atomics, and condition-variable use.
- Ensure callbacks do not outlive their connection, channel, or event loop.
- Check partial writes, backpressure, buffer growth, and idle or timeout paths.
- Verify errors propagate without leaking or terminating the loop.
