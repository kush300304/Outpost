# Benchmark summary

```
date: 2026-11-20T20:40:12Z
commit: 2cd93bf
cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
memory: 7.4Gi
kernel: 6.18.58.1-microsoft-standard-WSL2
docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
workload: 32 virtual users for 60s, 90% GET / 10% PUT over 1000 keys, through the router
rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)
storage: WAL fsync on every applied write (WAL_FSYNC=1)
```

Measured phase: 150758 requests, 1976 requests/s (k6 iterations; the 1000 setup PUTs are excluded).

| Latency (ms) | samples | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| GET, X-Cache HIT | 135668 | 1.06 | 1.45 | 106.97 | 10327.50 |
| GET, X-Cache MISS | 0 | – | – | – | – |
| GET, X-Cache STALE | 0 | – | – | – | – |
| PUT | 15090 | 33.13 | 42.10 | 766.42 | 10267.19 |

Cache hit ratio of GETs: 100.0%
Status codes: 2xx 150758, 404 0, 429 0, 5xx 0, other 0
k6 http_req_failed rate (includes setup): 0.0000%
