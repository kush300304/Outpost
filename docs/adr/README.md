# Architecture decision records

Each record explains one decision that shaped Outpost: the situation, what was decided, the alternatives, and
the consequences, good and bad. They are short on purpose; `docs/designWalkthrough.md` has the long form.

| # | Decision |
|---|---|
| [0001](0001-availability-over-consistency.md) | Availability over consistency during an origin outage (AP/BASE) |
| [0002](0002-versioned-last-writer-wins.md) | Versioned last-writer-wins writes for ordering and idempotent replay |
| [0003](0003-hand-built-write-ahead-log.md) | A hand-built write-ahead log for storage and the edge queues |
| [0004](0004-stateless-router-service.md) | A separate, stateless router service |
| [0005](0005-consistent-hashing-virtual-nodes.md) | Consistent hashing with 150 virtual nodes per edge |
| [0006](0006-circuit-breaker-thresholds.md) | Circuit breaker: 3 failures, 1 s heartbeat, 5 s reset, one probe |
| [0007](0007-group-commit-without-window.md) | Group commit without a batching window |
| [0008](0008-standard-http-caching-and-purge.md) | Standard HTTP caching with the write identity as ETag, and purge |
