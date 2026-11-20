# Hypothesis check: fewer clients than server workers

Same build as `2026-11-20T204012Z-benchmark`, with 8 virtual users (below the default pool size).
The cache-hit tail disappears (p99 1.75 ms, max 7.86 ms), which confirmed worker starvation as the
cause of the 32-client tail. Not a headline result.
