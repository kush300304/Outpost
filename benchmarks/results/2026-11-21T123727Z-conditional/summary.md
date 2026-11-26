# Benchmark summary

```
date: 2026-11-21T12:37:28Z
commit: 87e5989
cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
memory: 7.4Gi
kernel: 6.18.58.1-microsoft-standard-WSL2
docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
workload: conditional.js, 32 virtual users for 60s, PUT share 0.01 / 5% GET no-cache / the rest GET with If-None-Match once the client has an ETag, over 1000 keys, 4096-byte values, through the router
rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)
WAL fsync on every write: WAL_FSYNC=1 (1 = durable across power loss)
```

Measured phase: 358751 requests in 60s, 5979 requests/s (the 1000 setup PUTs are excluded). k6's own iteration rate, averaged over the whole run including the setup phase, is 5118/s; runs before 2026-11-21T1145 reported only that figure.

| Latency (ms) | samples | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| GET, If-None-Match matched: 304, no body | 240524 | 3.85 | 10.80 | 16.58 | 71.82 |
| GET, If-None-Match outdated: 200 + new value | 66634 | 4.08 | 11.46 | 17.80 | 48.09 |
| GET, no ETag yet: 200 HIT | 30028 | 4.55 | 11.02 | 14.88 | 37.17 |
| GET no-cache: REVALIDATED (origin 304) | 17981 | 6.62 | 15.39 | 22.33 | 65.22 |
| GET, X-Cache MISS | 2 | 6.98 | 7.62 | 7.68 | 7.69 |
| PUT | 3582 | 13.08 | 24.60 | 31.98 | 54.13 |

Conditional GETs answered 304: 240524 of 307158 (78.3%)
no-cache GETs revalidated with a 304 from the origin: 17981 of 17983 (100.0%)
Average body bytes: 304 0, 200 4126
Status codes: 2xx 118227, 304 240524, 404 0, 429 0, 5xx 0, other 0
k6 http_req_failed rate (includes setup): 0.0000%
Storage WAL during this run (setup PUTs included): 4583 appends, 4432 fsyncs (1.03 appends per fsync)
