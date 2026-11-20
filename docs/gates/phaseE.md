# Phase E exit gate: verification

Run: 2026-11-20T20:56:21Z on `0dba1bb`. Host: AMD Ryzen 5 5600H with Radeon Graphics, 12 threads, 7.4Gi RAM, WSL2 + Docker Desktop 29.9.3; g++ 15.2.0.

## 1. ASan + UBSan

```
$ cmake -S . -B build-asan -DOUTPOST_SANITIZE=address,undefined && cmake --build build-asan -j && ctest --test-dir build-asan
1/5 Test #1: common_tests .....................   Passed    0.18 sec
2/5 Test #2: storage_tests ....................   Passed    0.33 sec
3/5 Test #3: write_api_tests ..................   Passed    2.52 sec
4/5 Test #4: edge_tests .......................   Passed    7.91 sec
5/5 Test #5: router_tests .....................   Passed    2.90 sec
100% tests passed, 0 tests failed out of 5
sanitizer reports: 0
```

## 2. TSan

```
$ cmake -S . -B build-tsan -DOUTPOST_SANITIZE=thread && cmake --build build-tsan -j && ctest --test-dir build-tsan
1/5 Test #1: common_tests .....................   Passed    0.15 sec
2/5 Test #2: storage_tests ....................   Passed    0.36 sec
3/5 Test #3: write_api_tests ..................   Passed    2.56 sec
4/5 Test #4: edge_tests .......................   Passed    9.49 sec
5/5 Test #5: router_tests .....................   Passed    2.24 sec
100% tests passed, 0 tests failed out of 5
sanitizer reports: 0
```

## 3. Docker Compose

```
$ docker compose down -v && docker compose up -d --build --wait
 Container outpost-router-1 Healthy 
$ docker compose ps --format "table {{.Service}}\t{{.Status}}\t{{.Ports}}"
SERVICE     STATUS                   PORTS
edge-1      Up 4 seconds (healthy)   8081/tcp
edge-2      Up 4 seconds (healthy)   8081/tcp
edge-3      Up 4 seconds (healthy)   8081/tcp
router      Up 2 seconds (healthy)   0.0.0.0:8080->8080/tcp, [::]:8080->8080/tcp
storage     Up 9 seconds (healthy)   9091/tcp
write-api   Up 7 seconds (healthy)   9090/tcp
$ docker compose ps --format "{{.Publishers}}" | grep -o "PublishedPort:[1-9][0-9]*" | sort -u   # host-published ports
"PublishedPort":8080
```

The dashboard (host port 8090) is added in Phase F, so at this gate 8080 is the only published port.

## 4. Integration test

```
$ scripts/integrationTest.sh
PASS  router has 3 edges on the ring (got 3)
PASS  PUT served by the ring owner (edge-2 == edge-2)
PASS  storage holds the written value (hello)
PASS  read after write is a cache HIT (HIT)
PASS  cold read is a MISS, then HIT (MISS, HIT)
PASS  cold key is cached on its ring owner (edge-2 == edge-2)
PASS  key 'it-1795208226?a b/c%#+' stored exactly (it-1795208226?a b/c%#+ tricky)
PASS  burst from one client is rate-limited (    250 404     150 429 )
PASS  another client is still served (404)
PASS  every 429 carries Retry-After: 1 (150 of 150)
PASS  port 8081 is not published (curl got 000)
PASS  port 8082 is not published (curl got 000)
PASS  port 8083 is not published (curl got 000)
PASS  port 9090 is not published (curl got 000)
PASS  port 9091 is not published (curl got 000)
integrationTest: 0 failed check(s)
exit status: 0
```

## 5. Chaos test

```
$ scripts/chaosTest.sh
1795208228995 start run=chaos1795208228
1795208231737 seeded 200 warm keys
1795208237123 OUTAGE-START storage killed
1795208239590 all breakers OPEN (storage down) after 2454 ms
1795208247964 storage restarted
1795208254014 all breakers CLOSED and queues drained after 6036 ms
1795208254022 OUTAGE-END storage
1795208257538 OUTAGE-START write-api killed
1795208258258 all breakers OPEN (write-api down) after 702 ms
1795208266698 write-api restarted
1795208275564 all breakers CLOSED and queues drained after 8854 ms
1795208275570 OUTAGE-END write-api
1795208279058 write-api killed to queue writes
1795208291291 edge-1 pending=225
1795208291514 edge-2 pending=255
1795208291747 edge-3 pending=231
1795208292429 write-api restarted; edge-2 (pending=255) will be killed 200 ms into its drain
1795208293999 EDGE-KILL edge-2 killed mid-drain (queued at drain start=282; container now: exited exit=137)
1795208296478 edge-2 restarted
1795208300420 all breakers CLOSED and queues drained after 3929 ms
1795208300561 EDGE-KILL-END
1795208304146 final drain after 554 ms
1795208304152 load stopped
writes sent: 3391  acknowledged (200/202): 3388  not acknowledged: {'503': 3}
acknowledged writes persisted with the exact value: 3388 / 3388
reads of warm keys: 4396 total; during storage/write-api outages: 2112, failed: 0, served STALE: 1626
reads during the edge-kill window (reported, not asserted): 348, failed: 0
storage puts since its last restart: {'applied': 3126, 'duplicates': 41, 'superseded': 0} (duplicates = queued writes replayed after the mid-drain edge kill, applied only once)
RESULT: PASS
chaos results: benchmarks/results/2026-11-20T205708Z-chaos
exit status: 0
```

## 6. Benchmark

Headline run `benchmarks/results/2026-11-20T205135Z-benchmark` (built at `11cd021`; later commits change only docs, scripts and compose overrides):

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

Cache-hit p50 1.07 ms < 10 ms (spec latency NFR). Full analysis: `docs/benchmarks.md`.

## 7. Style

```
$ python3 scripts/checkStyle.py && scripts/format.sh --check
checkStyle: 44 files, 0 violations
clang-format: 44 files, all formatted
```

## 8. CI on GitHub Actions

First run of `.github/workflows/ci.yml` after the Phase E push (run `44816257622`, commit `d9d7d0f`, ubuntu-24.04 runners):

```
$ gh run view 44816257622 --json conclusion,jobs
success
sanitizers (thread): success
compose-integration: success
style: success
sanitizers (address,undefined): success
build-and-test: success
```

The run printed a deprecation notice for `actions/checkout@v4` (Node.js 20); the workflow moves to `actions/checkout@v5`.
