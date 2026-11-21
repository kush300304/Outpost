# Benchmark summary

```
date: 2026-11-21T11:43:11Z
commit: 4220ba3
cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
memory: 7.4Gi
kernel: 6.18.58.1-microsoft-standard-WSL2
docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
workload: readHeavy.js, fixed arrival rate 2345 requests/s (up to 128 virtual users) for 60s, 90% GET / 10% PUT over 1000 keys, through the router
rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)
WAL fsync on every write: WAL_FSYNC=1 (1 = durable across power loss)
```

Measured phase: 140702 requests, 2128 requests/s (k6 iterations; the 1000 setup PUTs are excluded).

| Latency (ms) | samples | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| GET, X-Cache HIT | 126633 | 0.95 | 1.24 | 1.94 | 17.73 |
| GET, X-Cache MISS | 0 | – | – | – | – |
| GET, X-Cache STALE | 0 | – | – | – | – |
| PUT | 14069 | 7.60 | 10.33 | 12.83 | 28.52 |

Cache hit ratio of GETs: 100.0%
Status codes: 2xx 140702, 404 0, 429 0, 5xx 0, other 0
k6 http_req_failed rate (includes setup): 0.0000%
Storage WAL during this run (setup PUTs included): 15069 appends, 11847 fsyncs (1.27 appends per fsync)
