# Run before the worker-pool fix

Measured at `2f9c7c8` (TCP_NODELAY fixed), before `fad4267` raised the cpp-httplib worker pool to 64.
32 keep-alive clients outnumbered the default pool (CPU count - 1), so requests queued behind idle
connections: cache-hit p99 106.97 ms, max 10.3 s. Superseded by `2026-11-20T205135Z-benchmark`.
