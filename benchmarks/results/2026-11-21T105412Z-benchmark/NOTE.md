# Note on this run's WAL line

The "Storage WAL (whole run, setup included)" line in `summary.md` is mislabelled. Storage was not restarted
between `2026-11-21T105221Z-benchmark` and this run, so `storageStats.json` holds counters for **both** runs:
84137 appends = (42778 + 1000) + (39359 + 1000) PUTs, with 27741 fsyncs (3.03 appends per fsync over both
runs). The latency and throughput numbers of this run are unaffected. `scripts/runBenchmark.sh` now records
before/after snapshots and reports the difference for a single run.
