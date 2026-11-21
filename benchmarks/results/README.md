# Results index

Every directory is one real run. `summary.md` holds the numbers; `environment.txt` holds the machine and
settings; `k6-summary.json` holds k6's raw output; chaos runs keep their raw logs.

| Run | What it is |
|---|---|
| `2026-11-21T113220Z-conditional` | **Conditional requests**, gate J: 1 % PUTs, 77.4 % of conditional GETs answered 304 |
| `2026-11-21T113028Z-conditional` | Conditional requests, gate J: 10 % PUTs (high churn), 26.2 % answered 304 |
| `2026-11-21T105727Z-chaos` | **Chaos test**, gate I (group commit): 3448/3448 persisted |
| `2026-11-21T105557Z-benchmark` | **Benchmark**, gate I (group commit): PUT p50 12.70 ms, 2.99 writes per fsync |
| `2026-11-21T105412Z-benchmark` | Gate I repeat run (its WAL line spans two runs, see `NOTE.md`) |
| `2026-11-21T105221Z-benchmark` | Gate I first run (before the script recorded WAL fsyncs) |
| `2026-11-20T212929Z-chaos` | **Chaos test**, final check from a fresh clone (`docs/finalCheck.md`) |
| `2026-11-20T205708Z-chaos` | **Chaos test** (gate E, cited in README): storage, write-api and a mid-drain edge killed under load |
| `2026-11-20T202426Z-chaos` | Chaos test on an earlier build (before the Nagle and worker-pool fixes) |
| `2026-11-20T213128Z-benchmark` | **Benchmark**, final check from a fresh clone (`docs/finalCheck.md`) |
| `2026-11-20T205135Z-benchmark` | **Benchmark** (gate E, cited in README): 32 clients, 60 s, durable writes (fsync on) |
| `2026-11-20T205318Z-benchmark` | Comparison: same workload with fsync off |
| `2026-11-20T204203Z-benchmark` | Diagnostic: 8 clients, before the worker-pool fix |
| `2026-11-20T204012Z-benchmark` | Superseded: before the worker-pool fix (cache-hit p99 107 ms) |
| `2026-11-20T202711Z-benchmark` | Superseded: trial before the TCP_NODELAY fix (cache-hit p50 44 ms) |
