# Phase D exit gate: remaining v1

Run: 2026-11-20T19:58:56Z on `d15acae`. Host: AMD Ryzen 5 5600H with Radeon Graphics, 12 threads, WSL2; g++ 15.2.0.

## 1. Build + ctest (includes the stats-schema test)

```
$ cmake --build build -j && ctest --test-dir build
1/5 Test #1: common_tests .....................   Passed    0.03 sec
2/5 Test #2: storage_tests ....................   Passed    0.05 sec
3/5 Test #3: write_api_tests ..................   Passed    0.44 sec
4/5 Test #4: edge_tests .......................   Passed    4.77 sec
5/5 Test #5: router_tests .....................   Passed    0.75 sec
100% tests passed, 0 tests failed out of 5

$ ./build/edge/edge_tests --gtest_filter='*Stats*:*Expired*:*InvalidWrite*:*Concurrent*:LruCacheTest.*'
[==========] Running 25 tests from 3 test suites.
[       OK ] LruCacheTest.GetOnEmptyReturnsNullopt (0 ms)
[       OK ] LruCacheTest.PutThenGetReturnsValue (0 ms)
[       OK ] LruCacheTest.PutOverwritesExistingKey (0 ms)
[       OK ] LruCacheTest.EvictsLeastRecentlyUsedWhenFull (0 ms)
[       OK ] LruCacheTest.GetPromotesToMRU (0 ms)
[       OK ] LruCacheTest.PutExistingKeyPromotesToMRU (0 ms)
[       OK ] LruCacheTest.ContainsDoesNotPromote (0 ms)
[       OK ] LruCacheTest.RemoveEntry (0 ms)
[       OK ] LruCacheTest.RemoveNonexistentReturnsFalse (0 ms)
[       OK ] LruCacheTest.ClearResetsEverything (0 ms)
[       OK ] LruCacheTest.StatsTracking (0 ms)
[       OK ] LruCacheTest.ZeroCapacityThrows (0 ms)
[       OK ] LruCacheTest.CapacityOneWorks (0 ms)
[       OK ] LruCacheTest.ConcurrentAccess (16 ms)
[       OK ] LruCacheTest.EntryExpiresAfterTtlButStaysAvailableAsStale (70 ms)
[       OK ] LruCacheTest.ZeroTtlNeverExpires (20 ms)
[       OK ] LruCacheTest.PutRefreshesAge (80 ms)
[       OK ] LruCacheTest.PutWithOlderVersionIsIgnored (0 ms)
[       OK ] LruCacheTest.ExpiredLookupCountsAsMiss (25 ms)
[       OK ] TokenBucketTest.StatsTracking (0 ms)
[       OK ] EdgeServerTest.InvalidWriteIsRejectedUpFrontEvenWhileDegraded (180 ms)
[       OK ] EdgeServerTest.ExpiredEntryIsRefetchedWhileClosed (155 ms)
[       OK ] EdgeServerTest.ExpiredEntryIsServedStaleWhenOriginFails (155 ms)
[       OK ] EdgeServerTest.ConcurrentRequestsThroughTheEdgeAreRaceFree (38 ms)
[       OK ] EdgeServerTest.StatsMatchTheDocumentedSchema (313 ms)
[==========] 25 tests from 3 test suites ran. (1056 ms total)
[  PASSED  ] 25 tests.

$ ./build/common/common_tests --gtest_filter='ShutdownSignal*:*Recovered*'
[==========] Running 3 tests from 2 test suites.
[       OK ] WalTest.RecoveredEntriesMatchTheLogAndAreTakenOnce (0 ms)
[       OK ] ShutdownSignalTest.RunsCallbackOnceWhenTheProcessGetsSigterm (0 ms)
[       OK ] ShutdownSignalTest.DestroyingWithoutASignalDoesNotCallBackOrHang (0 ms)
[==========] 3 tests from 2 test suites ran. (0 ms total)
[  PASSED  ] 3 tests.
```

## 2. Style over all project sources

```
$ python3 scripts/checkStyle.py
checkStyle: 44 files, 0 violations
$ scripts/format.sh --check
clang-format: 44 files, all formatted
```

## 3. Graceful shutdown

```
$ scripts/shutdownTest.sh
PASS storage: exit 0 after 109 ms
PASS write-api: exit 0 after 108 ms
PASS edge: exit 0 after 108 ms
PASS router: exit 0 after 107 ms
shutdownTest: all services shut down cleanly
```

## Extra: edge suite under ThreadSanitizer

```
$ cmake --build build-tsan -j --target edge_tests && ./build-tsan/edge/edge_tests   # build-tsan: -fsanitize=thread
      1 [  PASSED  ] 73 tests.
ThreadSanitizer reports: 0
```

## Start-up measurement for R11 (storage reads its WAL once)

Probe: build a 300,000-record WAL (46,733,340 bytes) and time `KvStore` construction three times, linked against the Debug-built `storage_lib` and `outpost_common`. Before `c7ac693`: 7232 / 7195 / 7147 ms. After: 3777 / 3742 / 3738 ms. Separately, WAL open + recovery alone took 3480 ms and a second `replay()` 3423 ms, which is the double read that was removed.
