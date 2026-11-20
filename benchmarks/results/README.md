# Results index

Every directory is one real run. `summary.md` holds the numbers; `environment.txt` holds the machine and
settings; `k6-summary.json` holds k6's raw output; chaos runs keep their raw logs.

| Run | What it is |
|---|---|
| `2026-11-20T202426Z-chaos` | **Chaos test** (headline): storage, write-api and a mid-drain edge killed under load |
| `2026-11-20T205135Z-benchmark` | **Benchmark** (headline): 32 clients, 60 s, durable writes (fsync on) |
| `2026-11-20T205318Z-benchmark` | Comparison: same workload with fsync off |
| `2026-11-20T204203Z-benchmark` | Diagnostic: 8 clients, before the worker-pool fix |
| `2026-11-20T204012Z-benchmark` | Superseded: before the worker-pool fix (cache-hit p99 107 ms) |
| `2026-11-20T202711Z-benchmark` | Superseded: trial before the TCP_NODELAY fix (cache-hit p50 44 ms) |
