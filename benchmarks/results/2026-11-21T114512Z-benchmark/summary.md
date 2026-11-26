# Benchmark summary

```
date: 2026-11-21T11:45:12Z
commit: 254eb22
cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
memory: 7.4Gi
kernel: 6.18.58.1-microsoft-standard-WSL2
docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
workload: readHeavy.js, fixed arrival rate 2587 requests/s (up to 128 virtual users) for 60s, 90% GET / 10% PUT over 1000 keys, through the router
rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)
WAL fsync on every write: WAL_FSYNC=1 (1 = durable across power loss)
```

Measured phase: 155206 requests in 60s, 2587 requests/s (the 1000 setup PUTs are excluded). k6's own iteration rate, averaged over the whole run including the setup phase, is 2346/s; runs before 2026-11-21T1145 reported only that figure.

| Latency (ms) | samples | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| GET, X-Cache HIT | 139763 | 0.98 | 1.36 | 2.45 | 20.39 |
| GET, X-Cache MISS | 0 | – | – | – | – |
| GET, X-Cache STALE | 0 | – | – | – | – |
| PUT | 15443 | 7.90 | 10.82 | 13.92 | 32.61 |

Cache hit ratio of GETs: 100.0%
Status codes: 2xx 155206, 404 0, 429 0, 5xx 0, other 0
k6 http_req_failed rate (includes setup): 0.0000%
Requests k6 could not start on time (dropped_iterations): 15
Storage WAL during this run (setup PUTs included): 16443 appends, 12334 fsyncs (1.33 appends per fsync)
