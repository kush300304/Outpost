# Phase B exit gate: failure detection

Run: 2026-11-19T19:45:01Z on `77df0f1`. Host: AMD Ryzen 5 5600H with Radeon Graphics, 12 threads, WSL2.

## 1. Build + ctest

```
$ cmake --build build -j && ctest --test-dir build
1/4 Test #1: common_tests .....................   Passed    0.03 sec
2/4 Test #2: storage_tests ....................   Passed    0.05 sec
3/4 Test #3: write_api_tests ..................   Passed    0.02 sec
4/4 Test #4: edge_tests .......................***Failed    3.08 sec
75% tests passed, 1 tests failed out of 4

$ ./build/edge/edge_tests | grep FAILED   # remaining failures are owned by Phase C
[  FAILED  ] ConsistentHashTest.KeysDistributeAcrossNodes
[  FAILED  ] LruCacheTest.ClearResetsEverything
```

## 2–3. Breaker, write-api and edge failure-detection tests

```
$ ./build/edge/edge_tests --gtest_filter='CircuitBreakerTest.*:EdgeServerTest.*'
[==========] Running 25 tests from 2 test suites.
[       OK ] CircuitBreakerTest.StartsInClosedState (0 ms)
[       OK ] CircuitBreakerTest.ClosedAllowsAllRequests (0 ms)
[       OK ] CircuitBreakerTest.ClosedToOpenAfterThreshold (0 ms)
[       OK ] CircuitBreakerTest.OpenRejectsRequests (0 ms)
[       OK ] CircuitBreakerTest.SuccessResetFailureCount (0 ms)
[       OK ] CircuitBreakerTest.OpenToHalfOpenAfterTimeout (120 ms)
[       OK ] CircuitBreakerTest.HalfOpenSuccessCloses (120 ms)
[       OK ] CircuitBreakerTest.HalfOpenFailureReopens (120 ms)
[       OK ] CircuitBreakerTest.HalfOpenLimitsProbes (120 ms)
[       OK ] CircuitBreakerTest.TransitionCallback (0 ms)
[       OK ] CircuitBreakerTest.ForceState (0 ms)
[       OK ] CircuitBreakerTest.FullLifecycleClosedOpenHalfOpenClosed (120 ms)
[       OK ] CircuitBreakerTest.LostProbeIsReadmittedAfterResetTimeout (240 ms)
[       OK ] CircuitBreakerTest.TransitionCallbackMayQueryBreaker (0 ms)
[       OK ] EdgeServerTest.WriteQueuedWhileBreakerStaysClosedIsDrained (57 ms)
[       OK ] EdgeServerTest.RestartedEdgeDrainsLeftoverQueue (65 ms)
[       OK ] EdgeServerTest.WriteQueuedDuringDrainIsNotLost (410 ms)
[       OK ] EdgeServerTest.OlderQueuedWriteNeverOverwritesNewerStoredValue (57 ms)
[       OK ] EdgeServerTest.ReplayAfterCrashBeforeDiscardIsIdempotent (113 ms)
[       OK ] EdgeServerTest.BreakerOpensAfterThresholdFailedHealthChecks (111 ms)
[       OK ] EdgeServerTest.OpenBreakerSendsNoOriginTrafficUntilResetTimeout (412 ms)
[       OK ] EdgeServerTest.HalfOpenAdmitsExactlyOneProbeBeforeClosing (608 ms)
[       OK ] EdgeServerTest.CachedReadWhileBreakerOpenIsStale (113 ms)
[       OK ] EdgeServerTest.UncachedReadWhenOriginFailsIs503NotNotFound (4 ms)
[       OK ] EdgeServerTest.MissingKeyIs404AndFoundKeyIsMissThenHit (6 ms)
[==========] 25 tests from 2 test suites ran. (2806 ms total)
[  PASSED  ] 25 tests.

$ ./build/write-api/write_api_tests
[==========] Running 6 tests from 3 test suites.
[       OK ] ApiHandlersTest.KeyLengthValidation (0 ms)
[       OK ] ApiHandlersTest.ValueSizeValidation (1 ms)
[       OK ] WriteApiHealthTest.HealthyWhenStorageAnswers (4 ms)
[       OK ] WriteApiHealthTest.UnhealthyWhenStorageUnreachable (2 ms)
[       OK ] WriteApiReadTest.MissingKeyIs404 (3 ms)
[       OK ] WriteApiReadTest.StorageFailureIs502NotNotFound (2 ms)
[==========] 6 tests from 3 test suites ran. (14 ms total)
[  PASSED  ] 6 tests.
```

## 4. Real binaries: storage outage and recovery

Settings: threshold 3, health interval 1000 ms, reset timeout 5000 ms. Bounds: OPEN within 3×1 s + 2 s = 5 s; CLOSED within 5 s + 1 s + 2 s = 8 s after storage restarts.

```
$ scripts/localCluster.sh start
storage started (pid 16967, port 9091)
write-api started (pid 16982, port 9090)
edge-1 started (pid 16997, port 8081)
edge-2 started (pid 17013, port 8082)
edge-3 started (pid 17029, port 8083)
$ curl -s -X PUT localhost:8081/key/demo -d hello
{"key":"demo","result":"applied","version":1795117510981100,"writerId":"edge-1"}
$ curl -s -i localhost:8081/key/demo | grep X-Cache
X-Cache: HIT
$ scripts/localCluster.sh kill storage
storage stopped (SIGKILL)
seconds until edge-1 breaker OPEN: 3.0
$ curl -s -i localhost:8081/key/demo | grep -E "HTTP|X-Cache"
HTTP/1.1 200 OK
X-Cache: STALE
$ curl -s -i localhost:8081/key/never-cached | grep -E "HTTP|X-Cache"
HTTP/1.1 503 Service Unavailable
X-Cache: MISS
$ curl -s -i -X PUT localhost:8081/key/queued -d during-outage | grep -E "HTTP|X-Write-Mode"
HTTP/1.1 202 Accepted
X-Write-Mode: QUEUED
$ scripts/localCluster.sh start storage
storage started (pid 17173, port 9091)
seconds until edge-1 breaker CLOSED: 4.9
$ curl -s localhost:9091/key/queued   # queued write reached storage
{"key":"queued","value":"during-outage","version":1795117514093708,"writerId":"edge-1"}
$ grep -E "Circuit breaker|Reconcil" build/localCluster/edge-1.log | cut -c1-140
[2026-11-19 19:45:13.948] [warning] [edge] [edge-1] Circuit breaker CLOSED -> OPEN
[2026-11-19 19:45:18.950] [warning] [edge] [edge-1] Circuit breaker OPEN -> HALF_OPEN
[2026-11-19 19:45:18.951] [warning] [edge] [edge-1] Circuit breaker HALF_OPEN -> CLOSED
[2026-11-19 19:45:18.951] [info] [edge] [edge-1] Reconciling 1 queued writes
[2026-11-19 19:45:18.962] [info] [edge] [edge-1] Reconciliation finished: 1 delivered, 0 rejected, 0 still queued
$ scripts/localCluster.sh stop
edge-3 stopped (SIGTERM)
edge-2 stopped (SIGTERM)
edge-1 stopped (SIGTERM)
write-api stopped (SIGTERM)
storage stopped (SIGTERM)
```

## 5. Style and dependency checks

```
$ python3 scripts/checkStyle.py $(git diff --name-only 3adb91e HEAD -- "*.h" "*.cpp")
checkStyle: 15 files, 0 violations
$ grep -nE "spdlog|nlohmann|httplib" common/include/common/circuitBreaker.h common/src/circuitBreaker.cpp
(no matches)
```
