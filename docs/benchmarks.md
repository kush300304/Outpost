# Benchmarks

All numbers below come from `scripts/runBenchmark.sh` runs saved under `benchmarks/results/`; see the
index there. Nothing is estimated.

## Method

- k6 0.49 in a container on the compose network, calling the router (client → router → edge →
  write-api → storage), so every request crosses at least two HTTP hops.
- Setup writes 1000 keys; then 32 virtual users loop for 60 s, 90 % `GET` / 10 % `PUT` over those
  keys. Each virtual user is its own client (`X-Forwarded-For`).
- The per-client rate limit is raised for the run so the limiter is not what is measured; with the
  default limit (100 tokens/s, burst 200 per client per edge) a single client is capped, which
  `scripts/integrationTest.sh` checks.

Machine and settings of the headline run:

```
date: 2026-11-20T20:51:35Z
commit: 11cd021
cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
memory: 7.4Gi
kernel: 6.18.58.1-microsoft-standard-WSL2
docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
workload: 32 virtual users for 60s, 90% GET / 10% PUT over 1000 keys, through the router
rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)
storage: WAL fsync on every applied write (WAL_FSYNC=1)
```

## Results

Throughput: **2286 requests/s** with durable writes (fsync on), 7360 requests/s with fsync off.

| Request | WAL fsync | p50 (ms) | p95 (ms) | p99 (ms) | max (ms) |
|---|---|---|---|---|---|
| GET, cache HIT | fsync on | 1.07 | 1.44 | 1.97 | 26.87 |
| GET, cache HIT | fsync off | 3.33 | 7.15 | 9.65 | 27.08 |
| PUT | fsync on | 115.58 | 126.30 | 220.44 | 345.46 |
| PUT | fsync off | 6.04 | 10.40 | 13.08 | 28.47 |

- Cache hits meet the spec's sub-10 ms target with durable writes: p99 1.97 ms.
- With fsync off, cache-hit latency rises (p50 3.33 ms) while the same machine serves 3.2× as many
  requests; both runs stay under 10 ms at p99.
- Writes pay for durability: storage applies writes one at a time under one lock and fsyncs each,
  so with 32 clients writes queue (PUT p50 116 ms vs 6 ms without fsync). Group commit
  (batching several writes per fsync) is listed in the v2 ideas.
- Every request in both runs returned 2xx (k6 `http_req_failed` 0 %). The cache-hit ratio is 100 %
  because the 1000 keys fit in the three edges' caches and the run is shorter than the 60 s TTL.

## Problems the benchmark found

1. **Nagle's algorithm** (`2cd93bf`): cpp-httplib leaves `TCP_NODELAY` off, so on keep-alive connections
   every request after the first waited ~40 ms for a delayed ACK (first trial: cache-hit p50 44 ms).
2. **Worker starvation** (`11cd021`): an idle keep-alive connection holds a server worker for up to 5 s,
   and the default pool is CPU count − 1, so 32 clients queued behind idle connections (cache-hit p99
   107 ms, max 10.3 s). The pool is now 64 workers.

Both have regression tests in `edge/tests/testEdgeServer.cpp`.
