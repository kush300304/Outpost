# Phase A exit gate: data safety

Run: 2026-11-19T19:34:00Z on branch `v1-completion` at `1045f43`.
Host: AMD Ryzen 5 5600H with Radeon Graphics, 12 threads, 7.4Gi RAM (WSL2); g++ 15.2.0, cmake 4.2.3.

## 1. Build + ctest

```
$ cmake --build build -j && ctest --test-dir build
[ 40%] Built target outpost_common
[ 48%] Built target common_tests
[ 55%] Built target storage_lib
[ 67%] Built target write_api_lib
[ 67%] Built target edge_lib
[ 75%] Built target storage_server
[ 75%] Built target storage_tests
[ 79%] Built target write_api_server
[ 87%] Built target write_api_tests
[ 87%] Built target edge_server
[100%] Built target edge_tests
1/4 Test #1: common_tests .....................   Passed    0.03 sec
2/4 Test #2: edge_tests .......................***Failed    6.48 sec
3/4 Test #3: write_api_tests ..................   Passed    0.00 sec
4/4 Test #4: storage_tests ....................   Passed    0.05 sec
75% tests passed, 1 tests failed out of 4

$ ./build/edge/edge_tests | grep FAILED   # failures are the three pre-existing ones owned by Phases B and C
[  FAILED  ] ConsistentHashTest.KeysDistributeAcrossNodes
[  FAILED  ] LruCacheTest.ClearResetsEverything
[  FAILED  ] CircuitBreakerTest.HalfOpenLimitsProbes
```

Result: **all Phase A tests pass**. `edge_tests` still contains three pre-existing failures from the first review that are exit items of later phases: `CircuitBreakerTest.HalfOpenLimitsProbes` (B), `ConsistentHashTest.KeysDistributeAcrossNodes` and `LruCacheTest.ClearResetsEverything` (C).

## 2. Required Phase A tests

```
$ ./build/common/common_tests
[==========] Running 22 tests from 1 test suite.
[       OK ] WalTest.AppendPutThenReplayReturnsSameEntry (0 ms)
[       OK ] WalTest.AppendDeleteThenReplay (0 ms)
[       OK ] WalTest.MultipleEntriesPreserveOrder (1 ms)
[       OK ] WalTest.SequenceNumbersAreMonotonicallyIncreasing (0 ms)
[       OK ] WalTest.SequenceContinuesAfterReopen (0 ms)
[       OK ] WalTest.ReplayOnNonexistentFileReturnsEmpty (0 ms)
[       OK ] WalTest.ReplayOnEmptyFileReturnsEmpty (0 ms)
[       OK ] WalTest.CorruptedEntryIsSkippedDuringReplay (0 ms)
[       OK ] WalTest.TruncateRemovesAllEntries (0 ms)
[       OK ] WalTest.CanAppendAfterTruncate (0 ms)
[       OK ] WalTest.EmptyKeyAndValue (0 ms)
[       OK ] WalTest.LargeEntry (10 ms)
[       OK ] WalTest.VersionAndWriterIdRoundTrip (0 ms)
[       OK ] WalTest.DiscardThroughKeepsEntriesAppendedAfterSnapshot (0 ms)
[       OK ] WalTest.RecoveryReportCountsTruncatedTail (0 ms)
[       OK ] WalTest.FailedAppendIsRolledBackAndLogStaysUsable (0 ms)
[       OK ] WalTest.Crc32ProducesConsistentResults (0 ms)
[       OK ] WalTest.Crc32DiffersForDifferentData (0 ms)
[       OK ] WalTest.ConcurrentAppendsDoNotCorruptLog (9 ms)
[       OK ] WalTest.TornTailThenAppendKeepsAllAcknowledgedWrites (0 ms)
[       OK ] WalTest.DamagedHeaderInMiddleKeepsLaterRecords (0 ms)
[       OK ] WalTest.SequenceKeepsIncreasingAfterTruncate (0 ms)
[==========] 22 tests from 1 test suite ran. (24 ms total)
[  PASSED  ] 22 tests.

$ ./build/storage/storage_tests
[==========] Running 14 tests from 2 test suites.
[       OK ] KvStoreTest.GetOnEmptyStoreReturnsNullopt (0 ms)
[       OK ] KvStoreTest.FirstPutIsAppliedWithItsVersion (0 ms)
[       OK ] KvStoreTest.NewerVersionOverwrites (0 ms)
[       OK ] KvStoreTest.SameVersionAndWriterIsDuplicate (0 ms)
[       OK ] KvStoreTest.OlderVersionNeverOverwritesNewerData (0 ms)
[       OK ] KvStoreTest.EqualVersionTieBreaksOnWriterId (0 ms)
[       OK ] KvStoreTest.GetAllKeysReturnsAllInsertedKeys (0 ms)
[       OK ] KvStoreTest.ForEachVisitsAllEntries (0 ms)
[       OK ] KvStoreTest.EmptyKeyAndLargeValue (1 ms)
[       OK ] KvStoreTest.ConcurrentWritesDoNotCorrupt (36 ms)
[       OK ] KvStoreTest.ConcurrentWritersConvergeOnHighestVersion (3 ms)
[       OK ] KvStoreDurableTest.StateSurvivesRestart (0 ms)
[       OK ] KvStoreDurableTest.OnlyAppliedWritesAreLogged (0 ms)
[       OK ] KvStoreDurableTest.ReplayIsIdempotentWhenLogRepeatsOrReordersRecords (0 ms)
[==========] 14 tests from 2 test suites ran. (42 ms total)
[  PASSED  ] 14 tests.

$ ./build/edge/edge_tests --gtest_filter='EdgeServerTest.*'
[==========] Running 5 tests from 1 test suite.
[       OK ] EdgeServerTest.WriteQueuedWhileBreakerStaysClosedIsDrained (56 ms)
[       OK ] EdgeServerTest.RestartedEdgeDrainsLeftoverQueue (55 ms)
[       OK ] EdgeServerTest.WriteQueuedDuringDrainIsNotLost (411 ms)
[       OK ] EdgeServerTest.OlderQueuedWriteNeverOverwritesNewerStoredValue (57 ms)
[       OK ] EdgeServerTest.ReplayAfterCrashBeforeDiscardIsIdempotent (117 ms)
[==========] 5 tests from 1 test suite ran. (698 ms total)
[  PASSED  ] 5 tests.
```

## 3. Style check on every C++ file changed in Phase A

```
$ python3 scripts/checkStyle.py $(git diff --name-only 7a8da2f HEAD -- "*.h" "*.cpp")
checkStyle: 19 files, 0 violations
```

## 4. No third-party code in the WAL

```
$ grep -rnE "spdlog|nlohmann|httplib" common/include/common/wal.h common/src/wal.cpp
(no matches)
```
