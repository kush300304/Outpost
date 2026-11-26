# Benchmark summary

```
date: 2026-11-21T12:34:24Z
commit: 87e5989
cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
memory: 7.4Gi
kernel: 6.18.58.1-microsoft-standard-WSL2
docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
workload: readHeavy.js, 32 virtual users for 60s, 90% GET / 10% PUT over 1000 keys, through the router
rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)
WAL fsync on every write: WAL_FSYNC=1 (1 = durable across power loss)
```

Measured phase: 357283 requests in 60s, 5955 requests/s (the 1000 setup PUTs are excluded). k6's own iteration rate, averaged over the whole run including the setup phase, is 5127/s; runs before 2026-11-21T1145 reported only that figure.

| Latency (ms) | samples | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| GET, X-Cache HIT | 321377 | 3.40 | 9.28 | 13.22 | 47.65 |
| GET, X-Cache MISS | 0 | – | – | – | – |
| GET, X-Cache STALE | 0 | – | – | – | – |
| PUT | 35906 | 13.59 | 21.18 | 26.89 | 52.42 |

Cache hit ratio of GETs: 100.0%
Status codes: 2xx 357283, 404 0, 429 0, 5xx 0, other 0
k6 http_req_failed rate (includes setup): 0.0000%
Storage WAL during this run (setup PUTs included): 36906 appends, 13405 fsyncs (2.75 appends per fsync)
