# Phase I exit gate: group commit

Run: 2026-11-21 on `a74a62c` (benchmarks and chaos) and `09a78c6` (tests). Host: AMD Ryzen 5 5600H with
Radeon Graphics, 12 threads, 7.4Gi RAM, WSL2 + Docker Desktop 29.9.3; g++ 15.2.0.

The images were built at `1597178`. The run files record `a74a62c`, which was HEAD during the runs and
differs from `1597178` only by clang-format reflow. Both already contain the Phase J edge and router changes
(HTTP caching headers, purge). Those do not touch the write path apart from adding an `ETag` header to PUT
responses.

Commits: WAL group commit `b5a5d59`, KvStore put without the map lock across the fsync `db79e1b`, WAL
truncate test and per-run fsync accounting `09a78c6`.

## 1. Tests in all three builds

New tests:
- `WalTest.ConcurrentAppendsShareFsyncs`: red before the change ("actual: 400 vs 400, every append paid for
  its own fsync").
- `WalTest.EveryAppendIsDurableWhenItReturns`.
- `WalTest.DiscardThroughDuringConcurrentAppendsKeepsLaterEntries`.
- `WalTest.TruncateDuringConcurrentAppendsLeavesAConsistentLog`.
- `KvStoreDurableTest.ConcurrentDurablePutsShareFsyncsAndConverge`: red before the KvStore change
  ("actual: 133 vs 133, durable puts did not share fsyncs").

Normal build:
```
$ ctest --test-dir build
1/5 Test #1: common_tests .....................   Passed    2.16 sec
2/5 Test #2: storage_tests ....................   Passed    0.10 sec
3/5 Test #3: write_api_tests ..................   Passed    0.61 sec
4/5 Test #4: edge_tests .......................   Passed    3.54 sec
5/5 Test #5: router_tests .....................   Passed    0.98 sec
100% tests passed, 0 tests failed out of 5
```

ASan + UBSan (`-DOUTPOST_SANITIZE=address,undefined`):
```
$ ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan
1/5 Test #1: common_tests .....................   Passed    4.32 sec
2/5 Test #2: storage_tests ....................   Passed    0.41 sec
3/5 Test #3: write_api_tests ..................   Passed    2.79 sec
4/5 Test #4: edge_tests .......................   Passed    5.15 sec
5/5 Test #5: router_tests .....................   Passed    3.55 sec
100% tests passed, 0 tests failed out of 5
sanitizer reports: 0
```

TSan (`-DOUTPOST_SANITIZE=thread`):
```
$ TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan
1/5 Test #1: common_tests .....................   Passed    6.45 sec
2/5 Test #2: storage_tests ....................   Passed    0.49 sec
3/5 Test #3: write_api_tests ..................   Passed    2.77 sec
4/5 Test #4: edge_tests .......................   Passed    4.65 sec
5/5 Test #5: router_tests .....................   Passed    2.71 sec
100% tests passed, 0 tests failed out of 5
sanitizer reports: 0
```

163 tests in total (140 at the final check).

**What the tests do not prove.** `discardThrough` and `truncate` wait until no fsync is in progress before
they touch the file. Without that wait, `discardThrough` could close the descriptor that a group-commit
leader is fsyncing with the lock released. That fsync would then fail with `EBADF` and mark the log failed,
or hit a reused descriptor. I removed each wait in turn and reran its concurrency test 5 times; both tests
still passed. The race window is only the few instructions between closing and reopening the file, and
forcing it would need fault injection around `fsync`. Durability is not at stake either way, because
`discardThrough` fsyncs every record it keeps into the new file. So the waits are there by design, and the
tests show only that both operations stay consistent under concurrent appends, TSan included.

## 2. Benchmark (fsync on), compared with the final-check run

`scripts/runBenchmark.sh 60s 32`: 32 virtual users for 60 s, 90 % GET / 10 % PUT over 1000 keys, through the
router, with every storage write fsync'd.

| Run | Commit | req/s | PUT p50 | PUT p95 | PUT p99 | GET HIT p50 | GET HIT p99 | appends per fsync |
|---|---|---|---|---|---|---|---|---|
| `2026-11-20T213128Z-benchmark` (before) | `09a8c2e` | 2345 | 113.90 | 123.73 | 214.76 | 0.96 | 1.64 | 1 (by design) |
| `2026-11-21T105221Z-benchmark` | `a74a62c` | 6478 | 12.33 | 17.92 | 21.43 | 2.80 | 10.24 | not recorded |
| `2026-11-21T105412Z-benchmark` | `a74a62c` | 5929 | 13.04 | 20.52 | 27.99 | 3.02 | 11.96 | see NOTE.md |
| `2026-11-21T105557Z-benchmark` | `a74a62c` + script change | 6123 | 12.70 | 18.91 | 23.76 | 2.95 | 11.29 | **2.99** |
| `2026-11-20T205318Z-benchmark` (fsync **off**, reference) | `11cd021` | 7360 | 6.04 | 10.40 | 13.08 | 3.33 | 9.65 | – |

Latencies are in ms. 0 failed requests and 0 non-2xx statuses in every run.

- **PUT p50 113.90 → 12.33–13.04 ms (≈ 9× lower); throughput 2345 → 5929–6478 req/s (≈ 2.6×), with
  every write still durable before it is acknowledged.** The last run's storage counters (before/after
  snapshots): 41498 appends, 13869 fsyncs, so 2.99 writes shared each fsync. 41498 = 40498 measured PUTs +
  1000 setup PUTs, an exact match.
- GET HIT latency went up (p50 0.96 → about 3 ms). The cause is the load generator, not the read path. k6
  runs a closed loop: each of the 32 users sends its next request only after the previous one finishes.
  Before, PUTs took about 114 ms, so most users were waiting on a write at any moment. Now they come back
  about 9× sooner and the machine serves 2.6× more requests, so GETs compete for the same 12 threads. The
  fsync-off reference run (old code, no Phase J) shows the same thing at a similar throughput: GET HIT p50
  3.33 ms at 7360 req/s.
- The second run's WAL line is mislabelled: storage was not restarted between runs, so its counters cover
  both of them (`2026-11-21T105412Z-benchmark/NOTE.md`). The script now snapshots before and after k6.
- The third run used the uncommitted benchmark-script change, committed in `09a78c6`. The services were
  unchanged.

## 3. Chaos test

`scripts/chaosTest.sh` → `benchmarks/results/2026-11-21T105727Z-chaos`:

```
writes sent: 3448  acknowledged (200/202): 3448  not acknowledged: {}
acknowledged writes persisted with the exact value: 3448 / 3448
reads of warm keys: 4275 total; during storage/write-api outages: 1689, failed: 0, served STALE: 1352
reads during the edge-kill window (reported, not asserted): 373, failed: 0
storage puts since its last restart: {'applied': 3189, 'duplicates': 50, 'superseded': 0}
RESULT: PASS
```

The edge kill was real: `edge-2 killed mid-drain (queued at drain start=340; container now: exited exit=137)`.

## 4. Style and format

```
$ python3 scripts/checkStyle.py
checkStyle: 47 files, 0 violations
$ scripts/format.sh --check
clang-format: 47 files, all formatted
```

## Result

Gate I passed.
