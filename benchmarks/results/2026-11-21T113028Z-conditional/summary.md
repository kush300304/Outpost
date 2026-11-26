# Benchmark summary

```
date: 2026-11-21T11:30:28Z
commit: 8009e7a
cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
memory: 7.4Gi
kernel: 6.18.58.1-microsoft-standard-WSL2
docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
workload: conditional.js, 32 virtual users for 60s, 85% GET with If-None-Match once the client has an ETag / 5% GET no-cache / 10% PUT over 1000 keys, 4096-byte values, through the router
rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)
WAL fsync on every write: WAL_FSYNC=1 (1 = durable across power loss)
```

Measured phase: 339587 requests, 5117 requests/s (k6 iterations; the 1000 setup PUTs are excluded).

| Latency (ms) | samples | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| GET, If-None-Match matched: 304, no body | 68659 | 3.26 | 9.21 | 13.42 | 73.61 |
| GET, If-None-Match outdated: 200 + new value | 192994 | 3.46 | 9.49 | 13.64 | 56.50 |
| GET, no ETag yet: 200 HIT | 27168 | 3.32 | 9.31 | 14.47 | 50.87 |
| GET no-cache: REVALIDATED (origin 304) | 16809 | 5.79 | 12.35 | 17.49 | 56.32 |
| GET, X-Cache MISS | 26 | 5.38 | 10.10 | 10.53 | 10.63 |
| PUT | 33931 | 14.37 | 22.68 | 30.34 | 126.26 |

Conditional GETs answered 304: 68659 of 261653 (26.2%)
no-cache GETs revalidated with a 304 from the origin: 16809 of 16835 (99.8%)
Average body bytes: 304 0, 200 4126
Status codes: 2xx 270928, 304 68659, 404 0, 429 0, 5xx 0, other 0
k6 http_req_failed rate (includes setup): 0.0000%
Storage WAL during this run (setup PUTs included): 34931 appends, 12809 fsyncs (2.73 appends per fsync)
