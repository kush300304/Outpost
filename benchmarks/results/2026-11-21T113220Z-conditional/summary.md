# Benchmark summary

```
date: 2026-11-21T11:32:20Z
commit: 6e517d3
cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
memory: 7.4Gi
kernel: 6.18.58.1-microsoft-standard-WSL2
docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
workload: conditional.js, 32 virtual users for 60s, PUT share 0.01 / 5% GET no-cache / the rest GET with If-None-Match once the client has an ETag, over 1000 keys, 4096-byte values, through the router
rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)
WAL fsync on every write: WAL_FSYNC=1 (1 = durable across power loss)
```

Measured phase: 495596 requests, 7467 requests/s (k6 iterations; the 1000 setup PUTs are excluded).

| Latency (ms) | samples | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| GET, If-None-Match matched: 304, no body | 337295 | 2.94 | 7.12 | 9.74 | 30.28 |
| GET, If-None-Match outdated: 200 + new value | 98280 | 3.14 | 7.34 | 10.04 | 25.33 |
| GET, no ETag yet: 200 HIT | 30048 | 3.18 | 7.64 | 10.50 | 23.38 |
| GET no-cache: REVALIDATED (origin 304) | 24960 | 5.01 | 9.50 | 12.30 | 32.56 |
| GET, X-Cache MISS | 2 | 8.10 | 10.79 | 11.03 | 11.09 |
| PUT | 5011 | 11.00 | 16.59 | 20.51 | 30.49 |

Conditional GETs answered 304: 337295 of 435575 (77.4%)
no-cache GETs revalidated with a 304 from the origin: 24960 of 24962 (100.0%)
Average body bytes: 304 0, 200 4126
Status codes: 2xx 158301, 304 337295, 404 0, 429 0, 5xx 0, other 0
k6 http_req_failed rate (includes setup): 0.0000%
Storage WAL during this run (setup PUTs included): 6011 appends, 5739 fsyncs (1.05 appends per fsync)
