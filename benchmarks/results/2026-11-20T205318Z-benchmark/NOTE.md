# Comparison only: per-write fsync disabled

`WAL_FSYNC=0` for storage and the edge queues. Writes survive a process crash but not a power loss
or kernel crash, so this is not a supported setting; it measures what per-write fsync costs.
Same build as the headline run `2026-11-20T205135Z-benchmark`.
