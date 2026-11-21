# Results index

Every directory is one real run. `summary.md` holds the numbers; `environment.txt` holds the machine and
settings; `k6-summary.json` holds k6's raw output; chaos runs keep their raw logs.

**Requests per second.** Runs before `2026-11-21T1145` report k6's iteration rate, which k6 averages over the
whole run *including* its ~6 s setup phase (1000 PUTs). The rate during the 60 s measured phase is about 10 %
higher: request count / 60 s (for example 155229 / 60 = 2587/s for the 2345/s final-check run). Later
summaries print both. Comparisons between runs are unaffected because every run used the same definition.

| Run | What it is |
|---|---|
| `2026-11-21T123727Z-conditional` | **Final check 2** (fresh clone): conditional requests, 1 % writes, 78.3 % answered 304 |
| `2026-11-21T123555Z-benchmark` | **Final check 2**: fixed 2587 req/s, PUT p50 8.76 ms, cache-hit p50 1.18 ms |
| `2026-11-21T123424Z-benchmark` | **Final check 2**: full speed, 5955 req/s |
| `2026-11-21T123241Z-chaos` | **Final check 2**: chaos PASS, 2834/2834 persisted |
| `2026-11-21T114512Z-benchmark` | **Same load as before group commit** (fixed 2587 req/s = the final-check run's measured-phase rate): PUT p50 113.90 → 7.90 ms, GET HIT p50 0.96 → 0.98 ms |
| `2026-11-21T114311Z-benchmark` | Fixed 2345 req/s, about 9 % below the baseline's real load (see `NOTE.md`) |
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
