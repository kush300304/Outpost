# Final checks

Each final check rebuilds and reruns everything from a **fresh clone** of the published branch, in an empty
directory: no build tree, dependency cache or Docker volume is reused. CI runs on GitHub for the same commit.

- [Final check 2](#final-check-2-after-phases-i-and-j-2026-11-21): after group commit (Phase I) and HTTP caching
  with purge (Phase J), on 2026-11-21.
- [Final check 1](#final-check-1-v1-2026-11-20): v1, on 2026-11-20.

**Machine:** AMD Ryzen 5 5600H (12 threads), 7.4 GiB RAM, Windows WSL2 kernel 6.18, Docker Desktop 29.9.3,
g++ 15.2.0. CI runs on GitHub's ubuntu-24.04 runners.

---

## Final check 2: after Phases I and J (2026-11-21)

The clone was `git clone --branch v1-completion <GitHub remote>` at **`67b2e40`**. The commits after it on the
branch change documentation only (`docs/handsOnTour.md`, the interview notes, README links).

### Summary

| Area | Result |
|---|---|
| Clean build + 163 tests (5 suites) | **PASS** (100 %) |
| ASan + UBSan | **PASS** (5/5 suites, 0 reports) |
| ThreadSanitizer | **PASS** (5/5 suites, 0 reports) |
| Code style (CONTRIBUTING.md) + clang-format | **PASS** (47 files, 0 violations, all formatted) |
| `docker compose up` from scratch (volumes removed) | **PASS** (7 services healthy in 19.8 s; only 8080 and 8090 published) |
| Integration test | **PASS** (21/21) |
| Dashboard acceptance | **PASS** (18/18) |
| Dashboard in a real browser (`dashboard/tests/browserClickTest.ps1`) | **PASS** (9/9 guided-demo steps, 0 console errors, no overflow at 1280 or 390 px) |
| Graceful shutdown | **PASS** (4/4 exit 0 in about 114 ms) |
| Chaos test | **PASS**: 2834/2834 acknowledged writes persisted, 0 failed warm reads in 1569 outage reads, 59 replayed duplicates recognised after a real mid-drain edge kill |
| Benchmarks | **PASS** (0 failed requests): 5955 req/s at full speed; at 2587 req/s PUT p50 8.76 ms and cache-hit p50 1.18 ms; 78.3 % of conditional reads answered 304 |
| CI (GitHub Actions, run 44903184983) | **PASS** (all 5 jobs) |

### 1. Build, tests, sanitizers, style

```
$ cmake -S . -B build && cmake --build build -j && ctest --test-dir build
real	2m43.896s
1/5 Test #1: common_tests .....................   Passed    1.53 sec
2/5 Test #2: storage_tests ....................   Passed    0.05 sec
3/5 Test #3: write_api_tests ..................   Passed    0.40 sec
4/5 Test #4: edge_tests .......................   Passed    3.70 sec
5/5 Test #5: router_tests .....................   Passed    1.41 sec
100% tests passed, 0 tests failed out of 5
163 tests in total
$ cmake -S . -B build-asan -DOUTPOST_SANITIZE=address,undefined && ... && ctest --test-dir build-asan
1/5 Test #1: common_tests .....................   Passed    2.18 sec
2/5 Test #2: storage_tests ....................   Passed    0.43 sec
3/5 Test #3: write_api_tests ..................   Passed    2.01 sec
4/5 Test #4: edge_tests .......................   Passed    6.12 sec
5/5 Test #5: router_tests .....................   Passed    4.83 sec
100% tests passed, 0 tests failed out of 5
asan+ubsan reports: 0
$ cmake -S . -B build-tsan -DOUTPOST_SANITIZE=thread && ... && ctest --test-dir build-tsan
1/5 Test #1: common_tests .....................   Passed    2.88 sec
2/5 Test #2: storage_tests ....................   Passed    0.42 sec
3/5 Test #3: write_api_tests ..................   Passed    2.02 sec
4/5 Test #4: edge_tests .......................   Passed    5.11 sec
5/5 Test #5: router_tests .....................   Passed    3.37 sec
100% tests passed, 0 tests failed out of 5
tsan reports: 0
$ python3 scripts/checkStyle.py && scripts/format.sh --check
checkStyle: 47 files, 0 violations
clang-format: 47 files, all formatted
```

### 2. Compose from scratch

```
$ docker compose down -v && docker compose up -d --build --wait
real	0m19.782s
SERVICE     STATUS                    PORTS
dashboard   Up 2 seconds (healthy)    0.0.0.0:8090->8090/tcp, [::]:8090->8090/tcp
edge-1      Up 7 seconds (healthy)    8081/tcp
edge-2      Up 7 seconds (healthy)    8081/tcp
edge-3      Up 7 seconds (healthy)    8081/tcp
router      Up 5 seconds (healthy)    0.0.0.0:8080->8080/tcp, [::]:8080->8080/tcp
storage     Up 12 seconds (healthy)   9091/tcp
write-api   Up 10 seconds (healthy)   9090/tcp
```

### 3. Integration, dashboard acceptance, browser, shutdown

```
$ scripts/integrationTest.sh
PASS  router has 3 edges on the ring (got 3)
PASS  PUT served by the ring owner (edge-2 == edge-2)
PASS  storage holds the written value (hello)
PASS  read after write is a cache HIT (HIT)
PASS  cold read is a MISS, then HIT (MISS, HIT)
PASS  cold key is cached on its ring owner (edge-3 == edge-3)
PASS  key 'it-1795264335?a b/c%#+' stored exactly (it-1795264335?a b/c%#+ tricky)
PASS  HIT carries ETag, Last-Modified and Age ("1795264337758532-edge-3")
PASS  Cache-Control has max-age and stale-if-error (max-age=60, stale-if-error=3600)
PASS  If-None-Match with the ETag is 304 without a body (304 0)
PASS  no-cache revalidates with the origin, which answers 304 (REVALIDATED)
PASS  purge reaches all 3 edges and evicts the key once (complete, edges, evicted: True 3 1)
PASS  read after purge is a MISS (MISS)
PASS  burst from one client is rate-limited (    275 404     125 429 )
PASS  another client is still served (404)
PASS  every 429 carries Retry-After: 1 (125 of 125)
PASS  port 8081 is not published (curl got 000)
PASS  port 8082 is not published (curl got 000)
PASS  port 8083 is not published (curl got 000)
PASS  port 9090 is not published (curl got 000)
PASS  port 9091 is not published (curl got 000)
integrationTest: 0 failed check(s)
```

```
$ scripts/dashboardAcceptance.sh
PASS  dashboard page is served
PASS  all 8 scripts are served as JavaScript modules
PASS  static files are revalidated on every load (Cache-Control: no-cache)
PASS  storage stats are proxied (True) but its keys are not (404)
PASS  cold GET is a MISS (404, MISS)
PASS  PUT is written directly (DIRECT via edge-2)
PASS  warm GET is a HIT (HIT)
PASS  conditional GET with the ETag is 304 (304)
PASS  purge through the dashboard proxy (complete=true), then the read is a MISS
PASS  simulated outage turned on ({"simulatedOutage":true,"storageHealthy":true})
PASS  all breakers OPEN after 2970 ms
PASS  cached read during the outage is STALE (200, STALE)
PASS  write during the outage is queued (202 QUEUED)
PASS  edge-1 shows the queued write (pending=1)
PASS  breakers CLOSED and queue drained after 4805 ms
PASS  queued write reached storage (during-outage)
PASS  event log has breaker and reconcile events (purge: accept-1795264343 was not cached | breaker: CLOSED -> OPEN | breaker: OPEN -> HALF_OPEN | breaker: HALF_OPEN -> CLOSED | reconcile: started: 1 queued writes | reconcile: finished: 1 delivered, 0 rejected, 0 still queued)
dashboardAcceptance: 0 failed check(s)
```

```
$ powershell.exe -File dashboard/tests/browserClickTest.ps1 -OutDir ...
guided demo finished after 9.2 s
1 passed: WRITTEN edge-3 wrote it through write-api to storage, which made it durable (fsync) before answering, and cached the new value.
2 passed: PURGED The router asked all 3 edges to drop the key; edge-3 had it cached and evicted it.
3 passed: MISS edge-3 had no copy, so it fetched the value from storage and cached it.
4 passed: HIT Served from edge-3's memory; the origin was not contacted.
5 passed: 304 Not Modified Your copy is current: edge-3 matched your If-None-Match to its ETag and sent no body.
6 passed: breakers OPEN Every breaker is OPEN: the edges now protect the origin and serve from cache.
7 passed: STALE The origin is unreachable, so edge-3 served its older copy instead of failing.
8 passed: QUEUED (202) The origin is unreachable, so edge-3 saved the write in its local write-ahead log and will deliver it once the origin is back.
9 passed: REVALIDATED Breakers closed, the queue drained, and storage confirmed the edge holds the write made during the outage.
flow caption: GET /key/demo-mwqeanrz REVALIDATED in 15.5 ms. edge-3 asked storage whether its copy is still current (If-None-Match); storage answered 304, so the value was not sent again.
screenshot demoDesktopLight: 1280x3458, horizontal overflow: False
load generator: 200 requests, 6 at a time. | HIT200 p509.7 ms p9515.1 ms p9916.2 ms
request tester: PUT -> WRITTENHTTP 200 · edge-2 · 16.6 ms | GET -> HITHTTP 200 · edge-2 · 7.2 ms | PURGE -> PURGEDHTTP 200 · no edge · 9.8 ms | GET -> MISSHTTP 200 · edge-2 · 8.2 ms
theme button: light -> dark -> auto
breaker filter: breaker
screenshot phoneLight: 390x8250, horizontal overflow: False
screenshot desktopDark: 1280x3543, horizontal overflow: False
screenshot phoneDark: 390x8250, horizontal overflow: False
console errors/warnings/exceptions: 0
cleanup: chrome stopped, profile removed: True
```

```
$ scripts/shutdownTest.sh
PASS storage: exit 0 after 114 ms
PASS write-api: exit 0 after 113 ms
PASS edge: exit 0 after 113 ms
PASS router: exit 0 after 114 ms
shutdownTest: all services shut down cleanly
```

### 4. Chaos test (`benchmarks/results/2026-11-21T123241Z-chaos`)

```
$ scripts/chaosTest.sh
1795264437521 load stopped
writes sent: 2835  acknowledged (200/202): 2834  not acknowledged: {'503': 1}
acknowledged writes persisted with the exact value: 2834 / 2834
reads of warm keys: 3557 total; during storage/write-api outages: 1569, failed: 0, served STALE: 1173
reads during the edge-kill window (reported, not asserted): 267, failed: 11
storage puts since its last restart: {'applied': 2664, 'duplicates': 59, 'superseded': 0} (duplicates = queued writes replayed after the mid-drain edge kill, applied only once)
RESULT: PASS
chaos results: benchmarks/results/2026-11-21T123241Z-chaos
```

Timeline:
- all breakers OPEN 2759 ms after storage was killed;
- all breakers OPEN 806 ms after write-api was killed;
- `edge-2 killed mid-drain (queued at drain start=261; container now: exited exit=137)`.

**Reported, not asserted.** These are outside the guarantees the test checks (acknowledged = persisted, no
failed warm read during the *origin* outages):
- 11 of the 267 reads made while the edge was being killed failed;
- one write was answered `503` and so was never acknowledged.

Earlier runs had 0 in that window. A request already in flight to an edge at the instant it dies is lost: the
router retries only a *refused* connection, not one cut in the middle of a response. That is listed in the
README's known limitations.

### 5. Benchmarks

Full speed (`benchmarks/results/2026-11-21T123424Z-benchmark`):
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
benchmark results: benchmarks/results/2026-11-21T123424Z-benchmark
```

Fixed 2587 requests/s, the same load as before group commit (`benchmarks/results/2026-11-21T123555Z-benchmark`):
```
Measured phase: 155222 requests in 60s, 2587 requests/s (the 1000 setup PUTs are excluded). k6's own iteration rate, averaged over the whole run including the setup phase, is 2230/s; runs before 2026-11-21T1145 reported only that figure.

| Latency (ms) | samples | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| GET, X-Cache HIT | 139630 | 1.18 | 2.00 | 3.76 | 17.75 |
| GET, X-Cache MISS | 0 | – | – | – | – |
| GET, X-Cache STALE | 0 | – | – | – | – |
| PUT | 15592 | 8.76 | 12.74 | 16.10 | 33.48 |

Cache hit ratio of GETs: 100.0%
Status codes: 2xx 155222, 404 0, 429 0, 5xx 0, other 0
k6 http_req_failed rate (includes setup): 0.0000%
Storage WAL during this run (setup PUTs included): 16592 appends, 12099 fsyncs (1.37 appends per fsync)
benchmark results: benchmarks/results/2026-11-21T123555Z-benchmark
```

Conditional requests, 1 % writes (`benchmarks/results/2026-11-21T123727Z-conditional`):
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
benchmark results: benchmarks/results/2026-11-21T123727Z-conditional
```

**These runs are 10–20 % slower than the gate runs earlier the same day** on the same laptop:

| | Gate runs | This check |
|---|---|---|
| Full speed | 6550–7138 req/s | 5955 req/s |
| At 2587/s: PUT p50 | 7.90 ms | 8.76 ms |
| At 2587/s: cache-hit p50 | 0.98 ms | 1.18 ms |
| Conditional 1 %: throughput | 8260 req/s | 5979 req/s |
| Conditional 1 %: 304 share | 77.4 % | 78.3 % |

Nothing in the code differs. A laptop under WSL2 and Docker Desktop varies with temperature and Windows
background work, so treat single numbers from it as ±20 %. The conclusions are unchanged:
- PUT p50 at the old load is about 13× better than the 113.90 ms before group commit;
- cache-hit p50 and p95 stay under 10 ms;
- the 304 share matches the churn model.

### 6. CI

Run 44903184983 for `67b2e40`: build-and-test, sanitizers (address,undefined), sanitizers (thread), style and
compose-integration all **success** (4 min 44 s).

### Still pending

- **The owner's own checks:** the keyboard and hover checks in `docs/testing.md` §3 that the click test can't
  do, and the hands-on tour (`docs/handsOnTour.md`).
- **Phase H** (publication cleanup) runs only when the owner says "run Phase H".
- **Merging** `v1-completion` into `main` needs the owner's approval.

---

## Final check 1: v1 (2026-11-20)


**Step 3 of the master prompt.** The final verification ran on 2026-11-20 against branch `v1-completion` at
commit `09a8c2e`. Two things were verified:
- a **fresh clone** (`git clone --branch v1-completion`) in an empty directory, with no build tree,
  dependency cache or Docker volume reused;
- CI on GitHub for the same commit.

**Machine:** AMD Ryzen 5 5600H (12 threads), 7.4 GiB RAM, Windows WSL2 kernel 6.18, Docker Desktop 29.9.3,
g++ 15.2.0, CMake 4.2.3. CI runs on GitHub's ubuntu-24.04 runners.

### Summary

| Area | Result |
|---|---|
| Clean build + 140 tests (5 suites) | **PASS** (100 %) |
| ASan + UBSan | **PASS** (5/5 suites, 0 reports) |
| ThreadSanitizer | **PASS** (5/5 suites, 0 reports) |
| Code style (CONTRIBUTING.md) + clang-format | **PASS** (44 files, 0 violations, all formatted) |
| `docker compose up` from the fresh clone | **PASS** (7 services healthy; only 8080 and 8090 published) |
| Spec demo sequence (real `docker compose stop storage`) | **PASS** |
| Integration test | **PASS** (15/15) |
| Dashboard acceptance (scripted, through the dashboard proxy) | **PASS** (13/13) |
| Graceful shutdown | **PASS** (4/4 exit 0 within 2 s) |
| Chaos test | **PASS**: 3714/3714 acknowledged writes persisted, 0 failed warm reads in 2476 outage reads, 59 replayed duplicates recognised after a real mid-drain edge kill |
| Benchmark | **PASS**: 2345 req/s; cache-hit p50 0.96 ms / p99 1.64 ms |
| CI (GitHub Actions) | **PASS** (all 5 jobs) |
| Dashboard in a real browser | **PENDING**: the owner's manual check (`docs/testing.md` §3) |

### 1. Fresh clone: build and tests

```
$ git clone --branch v1-completion <repo> fresh && cd fresh
09a8c2e Record Phase G gate results
$ cmake -S . -B build && cmake --build build -j && ctest --test-dir build
real	2m15.927s
1/5 Test #1: common_tests .....................   Passed    0.03 sec
2/5 Test #2: storage_tests ....................   Passed    0.06 sec
3/5 Test #3: write_api_tests ..................   Passed    0.57 sec
4/5 Test #4: edge_tests .......................   Passed    4.88 sec
5/5 Test #5: router_tests .....................   Passed    0.88 sec
100% tests passed, 0 tests failed out of 5
```

### 2. Sanitizers and style

```
$ cmake -S . -B build-asan -DOUTPOST_SANITIZE=address,undefined && cmake --build build-asan -j && ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan
1/5 Test #1: common_tests .....................   Passed    0.18 sec
2/5 Test #2: storage_tests ....................   Passed    0.33 sec
3/5 Test #3: write_api_tests ..................   Passed    2.68 sec
4/5 Test #4: edge_tests .......................   Passed    7.87 sec
5/5 Test #5: router_tests .....................   Passed    2.90 sec
100% tests passed, 0 tests failed out of 5
sanitizer reports: 0

$ cmake -S . -B build-tsan -DOUTPOST_SANITIZE=thread && cmake --build build-tsan -j && TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan
1/5 Test #1: common_tests .....................   Passed    0.15 sec
2/5 Test #2: storage_tests ....................   Passed    0.37 sec
3/5 Test #3: write_api_tests ..................   Passed    2.69 sec
4/5 Test #4: edge_tests .......................   Passed    9.53 sec
5/5 Test #5: router_tests .....................   Passed    2.25 sec
100% tests passed, 0 tests failed out of 5
sanitizer reports: 0

$ python3 scripts/checkStyle.py && scripts/format.sh --check
checkStyle: 44 files, 0 violations
clang-format: 44 files, all formatted
```

### 3. Docker Compose and the spec's demo sequence

```
$ docker compose down -v   (previous cluster and volumes removed)
$ docker compose up -d --build --wait
real	0m16.524s
$ docker compose ps
SERVICE     STATUS                    PORTS
dashboard   Up 2 seconds (healthy)    0.0.0.0:8090->8090/tcp, [::]:8090->8090/tcp
edge-1      Up 7 seconds (healthy)    8081/tcp
edge-2      Up 6 seconds (healthy)    8081/tcp
edge-3      Up 7 seconds (healthy)    8081/tcp
router      Up 4 seconds (healthy)    0.0.0.0:8080->8080/tcp, [::]:8080->8080/tcp
storage     Up 11 seconds (healthy)   9091/tcp
write-api   Up 9 seconds (healthy)    9090/tcp
# host-published ports:
"PublishedPort":8080
"PublishedPort":8090

# Spec demo sequence (curl through the router)
$ curl -i localhost:8080/key/foo
HTTP/1.1 404 Not Found
X-Node: edge-3
X-Cache: MISS
$ curl -i -X PUT localhost:8080/key/foo -d bar
HTTP/1.1 200 OK
X-Write-Mode: DIRECT
X-Node: edge-3
{"key":"foo","result":"applied","version":1795210124641460,"writerId":"edge-3"}
$ curl -i localhost:8080/key/foo
HTTP/1.1 200 OK
X-Node: edge-3
X-Cache: HIT
{"key":"foo","value":"bar"}
$ curl localhost:8080/debug/route/foo   → edge-3
# /debug/stats on edge-3 (cache hit rate, breaker state, tokens):
{'hitRate': 50.0, 'breaker': 'CLOSED', 'tokensRemaining': 200.0}
$ docker compose stop storage
edge-3 breaker OPEN after 5293 ms
$ curl -i localhost:8080/key/foo
HTTP/1.1 200 OK
X-Node: edge-3
X-Cache: STALE
{"key":"foo","value":"bar"}
$ curl -i -X PUT localhost:8080/key/foo -d during-outage
HTTP/1.1 202 Accepted
X-Write-Mode: QUEUED
X-Node: edge-3
$ docker compose start storage
edge-3 breaker CLOSED after 4495 ms
$ docker compose logs edge-3 | grep -E 'Circuit|Reconcil' | tail -5
[2026-11-20 21:28:50.309] [warning] [edge] [edge-3] Circuit breaker CLOSED -> OPEN
[2026-11-20 21:28:55.310] [warning] [edge] [edge-3] Circuit breaker OPEN -> HALF_OPEN
[2026-11-20 21:28:55.311] [warning] [edge] [edge-3] Circuit breaker HALF_OPEN -> CLOSED
[2026-11-20 21:28:55.312] [info] [edge] [edge-3] Reconciling 1 queued writes
[2026-11-20 21:28:55.325] [info] [edge] [edge-3] Reconciliation finished: 1 delivered, 0 rejected, 0 still queued
$ docker compose exec storage curl -s localhost:9091/key/foo
{"key":"foo","value":"during-outage","version":1795210130525479,"writerId":"edge-3"}
```

### 4. Integration, dashboard acceptance, graceful shutdown

```
$ scripts/integrationTest.sh
PASS  router has 3 edges on the ring (got 3)
PASS  PUT served by the ring owner (edge-2 == edge-2)
PASS  storage holds the written value (hello)
PASS  read after write is a cache HIT (HIT)
PASS  cold read is a MISS, then HIT (MISS, HIT)
PASS  cold key is cached on its ring owner (edge-1 == edge-1)
PASS  key 'it-1795210148?a b/c%#+' stored exactly (it-1795210148?a b/c%#+ tricky)
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

$ scripts/dashboardAcceptance.sh
PASS  dashboard page is served
PASS  cold GET is a MISS (404, MISS)
PASS  PUT is written directly (DIRECT via edge-1)
PASS  warm GET is a HIT (HIT)
PASS  simulated outage turned on ({"simulatedOutage":true,"storageHealthy":true})
PASS  all breakers OPEN after 2871 ms
PASS  cached read during the outage is STALE (200, STALE)
PASS  write during the outage is queued (202 QUEUED)
PASS  edge-2 shows the queued write (pending=1)
PASS  breakers CLOSED and queue drained after 4980 ms
PASS  queued write reached storage (during-outage)
PASS  event log has breaker and reconcile events (breaker: HALF_OPEN -> CLOSED | breaker: CLOSED -> OPEN | breaker: OPEN -> HALF_OPEN | breaker: HALF_OPEN -> CLOSED | reconcile: started: 1 queued writes | reconcile: finished: 1 delivered, 0 rejected, 0 still queued)
dashboardAcceptance: 0 failed check(s)
exit status: 0

$ scripts/shutdownTest.sh
PASS storage: exit 0 after 109 ms
PASS write-api: exit 0 after 108 ms
PASS edge: exit 0 after 109 ms
PASS router: exit 0 after 110 ms
shutdownTest: all services shut down cleanly
exit status: 0
```

### 5. Chaos test

Raw logs: `benchmarks/results/2026-11-20T212929Z-chaos/`.

```
$ scripts/chaosTest.sh
1795210170270 start run=chaos1795210169
1795210173026 seeded 200 warm keys
1795210178427 OUTAGE-START storage killed
1795210182010 all breakers OPEN (storage down) after 3570 ms
1795210190419 storage restarted
1795210198233 all breakers CLOSED and queues drained after 7803 ms
1795210198240 OUTAGE-END storage
1795210201759 OUTAGE-START write-api killed
1795210206123 all breakers OPEN (write-api down) after 4351 ms
1795210214569 write-api restarted
1795210222723 all breakers CLOSED and queues drained after 8143 ms
1795210222730 OUTAGE-END write-api
1795210226269 write-api killed to queue writes
1795210238537 edge-1 pending=214
1795210238773 edge-2 pending=259
1795210239001 edge-3 pending=234
1795210239726 write-api restarted; edge-2 (pending=259) will be killed 200 ms into its drain
1795210244188 EDGE-KILL edge-2 killed mid-drain (queued at drain start=352; container now: exited exit=137)
1795210246741 edge-2 restarted
1795210250822 all breakers CLOSED and queues drained after 4066 ms
1795210250968 EDGE-KILL-END
1795210254575 final drain after 571 ms
1795210254581 load stopped
writes sent: 3716  acknowledged (200/202): 3714  not acknowledged: {'503': 2}
acknowledged writes persisted with the exact value: 3714 / 3714
reads of warm keys: 4926 total; during storage/write-api outages: 2476, failed: 0, served STALE: 1772
reads during the edge-kill window (reported, not asserted): 333, failed: 0
storage puts since its last restart: {'applied': 3451, 'duplicates': 59, 'superseded': 0} (duplicates = queued writes replayed after the mid-drain edge kill, applied only once)
RESULT: PASS
chaos results: benchmarks/results/2026-11-20T212929Z-chaos
exit status: 0
```

### 6. Benchmark

Raw output: `benchmarks/results/2026-11-20T213128Z-benchmark/`.

```
$ scripts/runBenchmark.sh 60s 32
date: 2026-11-20T21:31:28Z
commit: 09a8c2e
cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
memory: 7.4Gi
kernel: 6.18.58.1-microsoft-standard-WSL2
docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
workload: 32 virtual users for 60s, 90% GET / 10% PUT over 1000 keys, through the router
rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)
WAL fsync on every write: WAL_FSYNC=1 (1 = durable across power loss)
# Benchmark summary

```
date: 2026-11-20T21:31:28Z
commit: 09a8c2e
cpu: AMD Ryzen 5 5600H with Radeon Graphics (12 threads visible)
memory: 7.4Gi
kernel: 6.18.58.1-microsoft-standard-WSL2
docker: 29.9.3, 12 CPUs, 7980466176 bytes for containers, Docker Desktop
workload: 32 virtual users for 60s, 90% GET / 10% PUT over 1000 keys, through the router
rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)
WAL fsync on every write: WAL_FSYNC=1 (1 = durable across power loss)
```

Measured phase: 155229 requests, 2345 requests/s (k6 iterations; the 1000 setup PUTs are excluded).

| Latency (ms) | samples | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| GET, X-Cache HIT | 139728 | 0.96 | 1.24 | 1.64 | 30.67 |
| GET, X-Cache MISS | 0 | – | – | – | – |
| GET, X-Cache STALE | 0 | – | – | – | – |
| PUT | 15501 | 113.90 | 123.73 | 214.76 | 286.53 |

Cache hit ratio of GETs: 100.0%
Status codes: 2xx 155229, 404 0, 429 0, 5xx 0, other 0
k6 http_req_failed rate (includes setup): 0.0000%
benchmark results: benchmarks/results/2026-11-20T213128Z-benchmark
exit status: 0
```

### 7. CI

```
$ gh run view 44819680690 --json headSha,conclusion,jobs
09a8c2effbab6e0a42d1934cb4acea79b8d62f71
success
sanitizers (thread): success
sanitizers (address,undefined): success
compose-integration: success
style: success
build-and-test: success
```

### Requirement-by-requirement

### Functional requirements (project spec)

| ID | Requirement | Result | Evidence |
|---|---|---|---|
| FR1 | Route a request to an edge deterministically via consistent hashing | **PASS** | §4 "PUT served by the ring owner"; `router_tests`; `/debug/route` |
| FR2 | Serve from cache; on a miss fetch from origin and populate the cache | **PASS** | §3 404 MISS → PUT → HIT; §4 "cold read is a MISS, then HIT … cached on its ring owner" |
| FR3 | Per-client rate limit at each edge | **PASS** | §4: 150 × 429 with `Retry-After: 1` for one client, another client still served |
| FR4 | Edge health-checks the origin; breaker opens after a consecutive-failure threshold | **PASS** | §3 OPEN 5.3 s after storage stopped; §5 OPEN 3.6 s / 4.4 s. Consecutive-failure threshold; the error-rate variant is not built (roadmap v2) |
| FR5 | While open: serve stale cache, queue writes | **PASS** | §3 `X-Cache: STALE`, `202 QUEUED`; §5 1772 STALE reads |
| FR6 | On recovery the breaker closes and queued writes reconcile | **PASS** | §3 log: `HALF_OPEN -> CLOSED`, `Reconciliation finished: 1 delivered`, storage holds the queued value |
| FR7 | Edges added or removed with bounded key remapping | **PASS** | Removal at runtime: router health-driven membership (`router_tests`, `docs/gates/phaseG.md` 1.8 s). Addition is a config change + router restart; adding a node moves 29.3 / 23.0 / 16.4 % (ideal 25 / 20 / 16.7), `docs/gates/phaseC.md` |

### Non-functional requirements

| Requirement | Result | Evidence |
|---|---|---|
| Availability: reads served with the origin fully down | **PASS** (cached keys) | §5 0 failed warm reads in 2476 outage reads. Uncached keys answer 503 by design |
| Scalability: adding a node remaps ≈ 1/N | **PASS** | 29.3 / 23.0 / 16.4 % vs 25 / 20 / 16.7 %; `ConsistentHashTest.AddingANodeMovesAboutOneOverNPlusOneOfKeys` |
| Latency: cache hits sub-10 ms; report measured p50/p95/p99 | **PASS** | §6 0.96 / 1.24 / 1.64 ms |
| Fault tolerance: zero write loss in degraded mode | **PASS** | §5 3714 / 3714 acknowledged writes persisted with the exact value |
| Consistency: BASE, eventually consistent | **PASS** (by design) | Versioned last-writer-wins in storage, cache ttl 60 s; `docs/architecture.md` |

### v1 scope and definition of done

| Item | Result | Evidence |
|---|---|---|
| 3 edges + write-api + storage, C++17, Docker Compose | **PASS** | §3 (plus the router and dashboard) |
| Storage = in-memory map + the same WAL as the edge queues | **PASS** | `storage/src/kvStore.cpp`, `common/src/wal.cpp` |
| Consistent hashing ring with virtual nodes | **PASS** | 150 per edge; `docs/gates/phaseC.md` |
| LRU cache per edge | **PASS** | `LruCacheTest.*` |
| Token-bucket rate limiter per edge | **PASS** | FR3 |
| Breaker closed/open/half-open driven by heartbeats | **PASS** | §3 log; `CircuitBreakerTest.*`, `EdgeServerTest.HalfOpenAdmitsExactlyOneProbeBeforeClosing` |
| Degraded mode and reconciliation | **PASS** | FR5, FR6 |
| Load test with real numbers in README | **PASS** | §6; README "Results" |
| Chaos test: kill the origin mid-load, reads keep working, no writes lost | **PASS** | §5 |
| Definition of done: one benchmark run, one chaos run, README with real numbers | **PASS** | §5, §6, README |

### Testing strategy (project spec)

| Item | Result | Evidence |
|---|---|---|
| Unit tests: hashing, LRU, token bucket, breaker transitions | **PASS** | §1 (140 tests in total) |
| Integration tests on the compose cluster | **PASS** | §4 |
| Chaos test with sent == persisted | **PASS** | §5 |
| Benchmark with real p50/p95/p99 | **PASS** | §6 |
| Idempotent replay check | **PASS** | §5: 59 duplicates recognised after the mid-drain kill; `EdgeServerTest.ReplayAfterCrashBeforeDiscardIsIdempotent` |

### Build guide and code style

| Item | Result | Evidence |
|---|---|---|
| Library boundary: the five primitives hand-built, no third-party code in `common/` | **PASS** | `docs/gates/phaseC.md` §6 |
| Naming conventions, header/source split | **PASS** | §2 checkStyle |
| Folder structure: README, CI, `scripts/{chaosTest,runBenchmark,seedData}.sh`, `benchmarks/results/`, `docs/{architecture,benchmarks}.md` | **PASS** | repository tree |
| `proto/`, `terraform/` | **N/A** | v2 and the post-v1 cloud step, out of scope |
| Code style in CONTRIBUTING.md (single exit, `@file`, no raw new/delete, timeouts, .clang-format) | **PASS** | §2 |

### Dashboard spec

| Item | Result | Evidence |
|---|---|---|
| Plain HTML/CSS/JS, one page, no framework, no build step, no external resources | **PASS** | `dashboard/`; `docs/gates/phaseF.md` §3 |
| nginx on 8090 proxying `/router`, `/edge1..3`, `/origin-admin` | **PASS** | §3 published ports, `docs/gates/phaseF.md` §1 |
| Polls `/debug/stats` every second; schema in `docs/statsApi.md`, covered by a test | **PASS** | `EdgeServerTest.StatsMatchTheDocumentedSchema` |
| Simulated outage via the write-api admin endpoint, only with `ADMIN_ENABLED=1` | **PASS** | `WriteApiAdminTest.*`; §4 |
| Acceptance 1: `docker compose up --build`, open :8090 | **PASS** | §3, §4 "dashboard page is served" |
| Acceptance 2: cold miss, warm hit, outage, STALE, queued write, recovery | **PASS** (scripted through the same proxy paths) / **PENDING** (from the UI by hand) | §4; `docs/testing.md` §3 |
| Acceptance 3: stats-API test passes; no console errors | **PASS** (test) / **PENDING** (console, manual) | |
| Light/dark themes, phone width, keyboard access | **PENDING** (manual) | implemented in `dashboard/styles.css` / `index.html`; check in `docs/testing.md` §3 |

### Still failing or pending

Nothing in the automated verification failed. Open items:

1. **The dashboard in a real browser has not been checked.** There is no browser in this environment, and the
   build guide's tool list has none. The owner's manual pass covers:
   - the acceptance steps from the UI alone;
   - the DevTools console;
   - dark theme, 375 px width, and keyboard-only use.

   The steps are in `docs/testing.md` §3, with a run log at the end.
2. **Known limitations, measured and documented, not failures:**
   - Writes cost about 114 ms at p50 under 32 concurrent clients, because storage fsyncs each write under one
     lock. Without fsync it is 6 ms (`docs/benchmarks.md`).
   - The breaker opens 3–5 s after an outage in compose. The low end is the 3 × 1 s heartbeat; when the dead
     peer's name must be looked up, each check can also spend up to the 1 s DNS bound.
   - The storage WAL is never compacted; there are no deletes; ordering relies on wall clocks; the router's
     edge list is static. These are listed in the README and `docs/roadmap.md`.
3. **Phase H (publication cleanup)** is defined in `docs/roadmap.md` and has **not** been run. It waits for
   the owner's "run Phase H".
