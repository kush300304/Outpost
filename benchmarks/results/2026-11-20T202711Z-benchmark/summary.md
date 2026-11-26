# Benchmark summary

date: 2026-11-20T20:27:11Z
commit: f9b464d
cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
memory: 7.4Gi
kernel: 6.18.58.1-microsoft-standard-WSL2
docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
workload: 16 virtual users for 20s, 90% GET / 10% PUT over 1000 keys, through the router
rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)
storage: WAL fsync on every applied write (WAL_FSYNC=1)

Requests: 5890 in the measured phase, 80 requests/s

| Latency (ms) | samples | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| all requests |  | 47.73 | 51.84 | 55.86 | 6914.68 |
| GET HIT |  | 43.92 | 47.95 | 48.32 | 6886.35 |
| GET MISS |  | 47.82 | 50.94 | 2229.68 | 6857.37 |
| GET STALE | 0 | – | – | – | – |
| PUT |  | 47.93 | 55.62 | 59.67 | 6914.68 |

Status codes: see k6-summary.json
Failed requests (k6 http_req_failed rate): 0.0000%
