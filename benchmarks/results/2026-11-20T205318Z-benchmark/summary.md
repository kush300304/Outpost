# Benchmark summary

```
date: 2026-11-20T20:53:18Z
commit: fad4267
cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
memory: 7.4Gi
kernel: 6.18.58.1-microsoft-standard-WSL2
docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
workload: 32 virtual users for 60s, 90% GET / 10% PUT over 1000 keys, through the router
rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)
WAL fsync on every write: WAL_FSYNC=0 (1 = durable across power loss)
```

Measured phase: 459282 requests, 7360 requests/s (k6 iterations; the 1000 setup PUTs are excluded).

| Latency (ms) | samples | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| GET, X-Cache HIT | 412934 | 3.33 | 7.15 | 9.65 | 27.08 |
| GET, X-Cache MISS | 0 | – | – | – | – |
| GET, X-Cache STALE | 0 | – | – | – | – |
| PUT | 46348 | 6.04 | 10.40 | 13.08 | 28.47 |

Cache hit ratio of GETs: 100.0%
Status codes: 2xx 459282, 404 0, 429 0, 5xx 0, other 0
k6 http_req_failed rate (includes setup): 0.0000%
