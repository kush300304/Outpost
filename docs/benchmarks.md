# Benchmarks

All numbers below come from `scripts/runBenchmark.sh` runs saved under `benchmarks/results/`. Each run has its
`environment.txt`, `summary.md` and k6's raw `k6-summary.json`; see the index there. Nothing is estimated.

## Method

- **Where the load comes from:** k6 0.49 runs in a container on the compose network and calls the router
  (client → router → edge → write-api → storage). Every request crosses at least two HTTP hops.
- **Setup:** writes the keys first (about 6 s); then the measured phase runs for 60 s. Each virtual user is its
  own client (`X-Forwarded-For`).
- **The rate limit is raised** for the run, so the limiter is not what is measured. With the default limit
  (100 tokens/s, burst 200 per client per edge) a single client is capped; `scripts/integrationTest.sh` checks
  that.
- **Storage fsyncs every write** before acknowledging it (`WAL_FSYNC=1`) unless a run says otherwise.
- **Workloads:**

  | Mode | Command | What it measures |
  |---|---|---|
  | Closed loop (default) | `scripts/runBenchmark.sh 60s 32` | 32 clients, each sending its next request as soon as the last returns: the most the machine can serve. 90 % `GET` / 10 % `PUT` over 1000 keys (`benchmarks/k6/readHeavy.js`). |
  | Fixed rate | `RATE=2587 scripts/runBenchmark.sh 60s 32` | The same mix started at a fixed number of requests per second, whatever the latency, so two builds can be compared at the same load. |
  | Conditional | `WORKLOAD=conditional WRITE_SHARE=0.01 scripts/runBenchmark.sh 60s 32` | HTTP caching with 4 KiB values. Each client sends the ETag it last saw in `If-None-Match`; 5 % of reads send `no-cache`; `WRITE_SHARE` of requests are PUTs (`benchmarks/k6/conditional.js`). |

- **Requests per second:** the number of requests in the 60 s measured phase divided by 60.
  - k6's own "iterations/s" averages over the whole run *including* the setup phase, so it is about 4–10 %
    lower.
  - Summaries before `2026-11-21T1145` printed only k6's figure, and the older gate files quote it. The tables
    below use the measured-phase rate throughout, computed from the recorded counts.
- **Machine:**

  ```
  cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
  memory: 7.4Gi
  kernel: 6.18.58.1-microsoft-standard-WSL2
  docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
  ```

## Results

### Before group commit: one fsync per write

| Run | Commit | Requests/s | GET HIT p50 / p95 / p99 | PUT p50 / p95 / p99 |
|---|---|---|---|---|
| `2026-11-20T205135Z` (gate E) | `11cd021` | 2527 | 1.07 / 1.44 / 1.97 | 115.58 / 126.30 / 220.44 |
| `2026-11-20T213128Z` (final check) | `09a8c2e` | 2587 | 0.96 / 1.24 / 1.64 | 113.90 / 123.73 / 214.76 |
| `2026-11-20T205318Z` (**fsync off**, reference) | `11cd021` | 7655 | 3.33 / 7.15 / 9.65 | 6.04 / 10.40 / 13.08 |

- **Why writes were slow:** storage applied writes one at a time under one lock and fsynced each one, so 32
  clients queued behind each other's fsyncs.
- **The cost of durability** was a PUT p50 of 114–116 ms with fsync, against 6 ms without it.

### Group commit (Phase I)

Concurrent writes now share one fsync, and storage no longer holds its map lock across the fsync.

**At the same load**: the final-check run's 2587 requests/s, replayed at a fixed rate on the new build:

| | GET HIT p50 / p95 / p99 | PUT p50 / p95 / p99 | Writes per fsync |
|---|---|---|---|
| Before (`2026-11-20T213128Z`) | 0.96 / 1.24 / 1.64 ms | 113.90 / 123.73 / 214.76 ms | 1 |
| After (`2026-11-21T114512Z`) | 0.98 / 1.36 / 2.45 ms | **7.90 / 10.82 / 13.92 ms** | 1.33 |

**At full speed** (closed loop, 32 clients):

| Run | Requests/s | GET HIT p50 / p95 / p99 | PUT p50 / p95 / p99 | Writes per fsync |
|---|---|---|---|---|
| `2026-11-21T105221Z` | 7138 | 2.80 / 7.46 / 10.24 | 12.33 / 17.92 / 21.43 | not recorded |
| `2026-11-21T105412Z` | 6550 | 3.02 / 8.26 / 11.96 | 13.04 / 20.52 / 27.99 | see its `NOTE.md` |
| `2026-11-21T105557Z` | 6759 | 2.95 / 8.03 / 11.29 | 12.70 / 18.91 / 23.76 | 2.99 |

- **Writes:** at the same load, PUT p50 is about 14× lower (113.90 → 7.90 ms). At full speed the machine serves
  2.6× more requests (2587 → 6759/s). Every write is still durable before it is acknowledged.
- **Reads at the same load** are unchanged at the median (+0.02 ms) and 0.8 ms higher at p99.
- **Reads at full speed got slower** because the load is 2.6× higher:
  - PUTs no longer hold the 32 clients up, so more GETs run at the same time on the same 12 threads.
  - The fsync-off reference run, at a similar rate, shows a similar GET p50 (3.33 ms).
  - At this load the **cache-hit p99 (10.24–11.96 ms) misses the spec's 10 ms target**; p50 and p95 meet it.
- **Group commit only helps when writes overlap:** 2.99 writes shared each fsync at full speed, 1.33 at the
  fixed 2587/s, and 1.05 in the 1 %-write conditional run. There is no batching window. A lone write is never
  delayed, so it is never batched either.

### HTTP caching (Phase J)

| Run | PUT share | Requests/s | Conditional GETs → 304 | 304 p50 / p99 | 200 with a new value p50 / p99 | REVALIDATED p50 / p99 | Body bytes 304 / 200 |
|---|---|---|---|---|---|---|---|
| `2026-11-21T113220Z` | 1 % | 8260 | 337295 / 435575 (**77.4 %**) | 2.94 / 9.74 ms | 3.14 / 10.04 ms | 5.01 / 12.30 ms | 0 / 4126 |
| `2026-11-21T113028Z` | 10 % | 5660 | 68659 / 261653 (26.2 %) | 3.26 / 13.42 ms | 3.46 / 13.64 ms | 5.79 / 17.49 ms | 0 / 4126 |

- **The 304 share depends on churn.** A conditional GET returns 304 only if nobody rewrote the key since that
  client last read it. With λ rewrites per key per second and a client revisiting a key every T seconds on
  average, about 1 / (1 + λT) of conditional reads find it unchanged:
  - 1 %: λ ≈ 0.075, T ≈ 4.6 s, so ≈ 75 % (measured 77.4 %);
  - 10 %: λ ≈ 0.51, T ≈ 7.4 s, so ≈ 21 % (measured 26.2 %).
- **The saving is bandwidth.** A 304 carries no body, against 4126 bytes for a 200. In the 1 % run that is
  337295 × 4126 B ≈ 1.39 GB not sent in 60 s. On one machine the latency difference is only about 0.2 ms; over
  a real network the saving is transfer time and egress cost.
- **Revalidation costs one origin round trip but no body:** REVALIDATED p50 is 5.0–5.8 ms against about 3 ms for
  an edge hit. `no-cache` reads were confirmed by a 304 from the origin 99.8–100 % of the time.

Every request in every run above returned 2xx or 304 (k6 `http_req_failed` 0 %). The cache-hit ratio of plain
GETs is 100 %, because the 1000 keys fit in the three edges' caches.

### Final check from a fresh clone (`docs/finalCheck.md`)

The same three workloads were rerun on the final code from a fresh clone, on the same laptop:

| Run | Requests/s | GET HIT p50 / p95 / p99 | PUT p50 / p95 / p99 | Note |
|---|---|---|---|---|
| `2026-11-21T123424Z` full speed | 5955 | 3.40 / 9.28 / 13.22 | 13.59 / 21.18 / 26.89 | 2.75 writes per fsync |
| `2026-11-21T123555Z` fixed 2587/s | 2587 | 1.18 / 2.00 / 3.76 | 8.76 / 12.74 / 16.10 | 1.37 writes per fsync |
| `2026-11-21T123727Z` conditional, 1 % writes | 5979 | 304: 3.85 / 10.80 / 16.58 | 13.08 / 24.60 / 31.98 | 78.3 % of conditional reads → 304 |

- **These are 10–20 % slower than the gate runs above**, with no code difference. A laptop under WSL2 and
  Docker Desktop varies with temperature and background load, so treat single numbers as ±20 %.
- **Every comparison still holds:**
  - PUT p50 at the old load is 8.76 ms against 113.90 ms before group commit;
  - cache-hit p50 and p95 stay under 10 ms;
  - the 304 share matches the churn model.

## Problems the benchmarks found

1. **Nagle's algorithm** (`2cd93bf`): cpp-httplib leaves `TCP_NODELAY` off, so on keep-alive connections
   every request after the first waited ~40 ms for a delayed ACK (first trial: cache-hit p50 44 ms).
2. **Worker starvation** (`11cd021`): an idle keep-alive connection holds a server worker for up to 5 s,
   and the default pool is CPU count − 1, so 32 clients queued behind idle connections (cache-hit p99
   107 ms, max 10.3 s). The pool is now 64 workers.
3. **Writes serialized behind fsync** (`b5a5d59`, `db79e1b`): the measured PUT p50 of 114 ms led to group commit.

Each of these has a regression test.

## Measurement lessons

- **Closed-loop load changes with the code.** Making writes faster made reads look slower, because the clients
  simply sent more requests. Compare builds at a fixed rate.
- **Know what a rate is averaged over:** k6's iteration rate includes its setup phase.
- **Cumulative counters need before/after snapshots.** One run's WAL fsync figure first covered two runs,
  because storage had not restarted between them (`2026-11-21T105412Z-benchmark/NOTE.md`).
