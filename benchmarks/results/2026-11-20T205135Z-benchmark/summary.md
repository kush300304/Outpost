# Benchmark summary

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

Measured phase: 151621 requests, 2286 requests/s (k6 iterations; the 1000 setup PUTs are excluded).

| Latency (ms) | samples | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| GET, X-Cache HIT | 136456 | 1.07 | 1.44 | 1.97 | 26.87 |
| GET, X-Cache MISS | 0 | – | – | – | – |
| GET, X-Cache STALE | 0 | – | – | – | – |
| PUT | 15165 | 115.58 | 126.30 | 220.44 | 345.46 |

Cache hit ratio of GETs: 100.0%
Status codes: 2xx 151621, 404 0, 429 0, 5xx 0, other 0
k6 http_req_failed rate (includes setup): 0.0000%
