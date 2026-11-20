# Phase C exit gate: wiring

Run: 2026-11-20T19:26:52Z on `9d84532`. Host: AMD Ryzen 5 5600H with Radeon Graphics, 12 threads, WSL2; g++ 15.2.0.

## 1. Build + ctest

```
$ cmake --build build -j && ctest --test-dir build
1/5 Test #1: common_tests .....................   Passed    0.03 sec
2/5 Test #2: storage_tests ....................   Passed    0.06 sec
3/5 Test #3: write_api_tests ..................   Passed    0.50 sec
4/5 Test #4: edge_tests .......................   Passed    3.92 sec
5/5 Test #5: router_tests .....................   Passed    0.78 sec
100% tests passed, 0 tests failed out of 5
```

## 2. Hash ring distribution and remapping

```
$ ./build/edge/edge_tests --gtest_filter='ConsistentHashTest.*'
[==========] Running 11 tests from 1 test suite.
[       OK ] ConsistentHashTest.EmptyRingReturnsEmptyString (0 ms)
[       OK ] ConsistentHashTest.SingleNodeGetsAllKeys (0 ms)
[       OK ] ConsistentHashTest.SameKeyAlwaysMapsToSameNode (0 ms)
[       OK ] ConsistentHashTest.KeysDistributeEvenlyAcrossNodes (83 ms)
[       OK ] ConsistentHashTest.AddingANodeMovesAboutOneOverNPlusOneOfKeys (521 ms)
[       OK ] ConsistentHashTest.RoutingDoesNotDependOnInsertionOrder (15 ms)
[       OK ] ConsistentHashTest.RemoveNodeRemapsOnlyItsKeys (22 ms)
[       OK ] ConsistentHashTest.AddNodeIdempotent (0 ms)
[       OK ] ConsistentHashTest.RemoveNonexistentNodeIsSafe (0 ms)
[       OK ] ConsistentHashTest.HasNodeAndGetNodes (0 ms)
[       OK ] ConsistentHashTest.HashDeterministic (0 ms)
[==========] 11 tests from 1 test suite ran. (644 ms total)
[  PASSED  ] 11 tests.

$ ./ringNumbers   # probe linked against build/common/liboutpost_common.a; source in the appendix
3 nodes, 100000 keys: edge-1 owns 32.0%
3 nodes, 100000 keys: edge-2 owns 36.2%
3 nodes, 100000 keys: edge-3 owns 31.9%
add node 3->4: 29.3% of keys moved (ideal 25.0%)
add node 4->5: 23.0% of keys moved (ideal 20.0%)
add node 5->6: 16.4% of keys moved (ideal 16.7%)
```

First review, same keys, before the fmix64 finalizer: 10.4 / 55.6 / 34.0 % split; adding a node moved 24.3 % (3→4), 3.9 % (4→5), 28.4 % (5→6).

## 3. Router tests

```
$ ./build/router/router_tests
[==========] Running 7 tests from 1 test suite.
[       OK ] RouterFixture.SameKeyAlwaysReachesTheSameEdgeNamedByDebugRoute (91 ms)
[       OK ] RouterFixture.KeysSpreadAcrossAllEdges (202 ms)
[       OK ] RouterFixture.StoppingAnEdgeMovesOnlyItsKeysAndRequestsKeepSucceeding (275 ms)
[       OK ] RouterFixture.RecoveredEdgeRejoinsTheRingAndGetsItsKeysBack (162 ms)
[       OK ] RouterFixture.ForwardsDecodedKeyClientAddressAndBody (8 ms)
[       OK ] RouterFixture.EdgeStatusAndHeadersPassThrough (9 ms)
[       OK ] RouterFixture.NoHealthyEdgeIs503 (8 ms)
[==========] 7 tests from 1 test suite ran. (758 ms total)
[  PASSED  ] 7 tests.
```

## 4. Rate limiter, URL encoding and write-api tests

```
$ ./build/edge/edge_tests --gtest_filter='TokenBucketTest.*:*Burst*:*TrackedClients*:*RejectsZero*:*ReservedCharacters*'
[==========] Running 13 tests from 2 test suites.
[       OK ] TokenBucketTest.StartsFullAllowsBurstToCapacity (0 ms)
[       OK ] TokenBucketTest.ThrottlesWhenEmpty (0 ms)
[       OK ] TokenBucketTest.RefillsOverTime (100 ms)
[       OK ] TokenBucketTest.DoesNotExceedCapacity (100 ms)
[       OK ] TokenBucketTest.TryConsumeMultiple (0 ms)
[       OK ] TokenBucketTest.TryConsumeMoreThanAvailable (0 ms)
[       OK ] TokenBucketTest.StatsTracking (0 ms)
[       OK ] TokenBucketTest.CapacityAndRefillRate (0 ms)
[       OK ] TokenBucketTest.RejectsInvalidConfiguration (0 ms)
[       OK ] EdgeServerTest.KeyWithReservedCharactersRoundTripsToStorage (10 ms)
[       OK ] EdgeServerTest.ClientOverBurstGets429WhileOtherClientIsServed (9 ms)
[       OK ] EdgeServerTest.TrackedClientsStayBoundedAndEvictedClientGetsFreshBucket (6 ms)
[       OK ] EdgeServerTest.RejectsZeroTrackedClients (0 ms)
[==========] 13 tests from 2 test suites ran. (227 ms total)
[  PASSED  ] 13 tests.

$ ./build/common/common_tests --gtest_filter='UrlEncodeTest.*'
[==========] Running 4 tests from 1 test suite.
[       OK ] UrlEncodeTest.KeepsUnreservedCharacters (0 ms)
[       OK ] UrlEncodeTest.EncodesReservedAndSpecialCharacters (0 ms)
[       OK ] UrlEncodeTest.EncodesUtf8BytesIndividually (0 ms)
[       OK ] UrlEncodeTest.EmptyStaysEmpty (0 ms)
[==========] 4 tests from 1 test suite ran. (0 ms total)
[  PASSED  ] 4 tests.

$ ./build/write-api/write_api_tests
[==========] Running 11 tests from 4 test suites.
[       OK ] WriteApiHealthTest.HealthyWhenStorageAnswers (4 ms)
[       OK ] WriteApiHealthTest.UnhealthyWhenStorageUnreachable (1 ms)
[       OK ] WriteApiReadTest.MissingKeyIs404 (4 ms)
[       OK ] WriteApiReadTest.StoredKeyIsReturnedWithItsVersion (4 ms)
[       OK ] WriteApiReadTest.StorageFailureIs502NotNotFound (2 ms)
[       OK ] WriteApiValidationTest.KeyAt256BytesIsAcceptedAndAt257Rejected (5 ms)
[       OK ] WriteApiValidationTest.ValueAtOneMebibyteIsAcceptedAndAboveRejected (380 ms)
[       OK ] WriteApiValidationTest.MissingOrInvalidVersionOrWriterIsRejected (6 ms)
[       OK ] WriteApiValidationTest.NonJsonBodyIsRejected (3 ms)
[       OK ] WriteApiWriteTest.ValidWriteIsAppliedThenDuplicateThenSuperseded (6 ms)
[       OK ] WriteApiWriteTest.StorageFailureOnWriteIs502 (2 ms)
[==========] 11 tests from 4 test suites ran. (422 ms total)
[  PASSED  ] 11 tests.
```

## 5. Real binaries through the router

```
$ scripts/localCluster.sh start
storage started (pid 25989, port 9091)
write-api started (pid 26004, port 9090)
edge-1 started (pid 26019, port 8081)
edge-2 started (pid 26035, port 8082)
edge-3 started (pid 26051, port 8083)
router started (pid 26067, port 8080)
$ curl -s -i -X PUT localhost:8080/key/a%3Fb%20c%2F%25%23%2B -d tricky        # key is: a?b c/%#+
HTTP/1.1 200 OK
X-Write-Mode: DIRECT
X-Node: edge-3
{"key":"a?b c/%#+","result":"applied","version":1795202822136618,"writerId":"edge-3"}
$ curl -s localhost:8080/debug/route/a%3Fb%20c%2F%25%23%2B
{"key":"a?b c/%#+","node":"edge-3"}
$ curl -s -i localhost:8080/key/a%3Fb%20c%2F%25%23%2B
HTTP/1.1 200 OK
X-Node: edge-3
X-Cache: HIT
{"key":"a?b c/%#+","value":"tricky"}
$ curl -s localhost:9091/key/a%3Fb%20c%2F%25%23%2B      # storage holds the exact key
{"key":"a?b c/%#+","value":"tricky","version":1795202822136618,"writerId":"edge-3"}

# 300 parallel GETs from one client (X-Forwarded-For 198.51.100.9), burst 200 per client per edge:
$ seq 300 | xargs -P 30 -I{} curl -s -o /dev/null -w "%{http_code}\n" -H "X-Forwarded-For: 198.51.100.9" localhost:8080/key/hot | sort | uniq -c
    238 404
     62 429
$ curl -s -o /dev/null -w "%{http_code}\n" -H "X-Forwarded-For: 198.51.100.10" localhost:8080/key/hot   # another client
404

# failover: kill the owner of key "hot", the router moves it to another edge
owner of hot: edge-1
$ scripts/localCluster.sh kill edge-1
edge-1 stopped (SIGKILL)
$ curl -s -i -H "X-Forwarded-For: 198.51.100.11" localhost:8080/key/hot
HTTP/1.1 404 Not Found
X-Node: edge-3
X-Cache: MISS
$ curl -s localhost:8080/debug/stats | grep -E "id|inRing|failovers"
      "id": "edge-1",
      "inRing": false,
      "id": "edge-2",
      "inRing": true,
      "id": "edge-3",
      "inRing": true,
  "failovers": 1,
$ scripts/localCluster.sh stop
router stopped (SIGTERM)
edge-3 stopped (SIGTERM)
edge-2 stopped (SIGTERM)
edge-1 not running
write-api stopped (SIGTERM)
storage stopped (SIGTERM)
```

## 6. Dependency and style checks

```
$ grep -rnE "spdlog|nlohmann|httplib" common/
(no matches)
$ python3 scripts/checkStyle.py $(git diff --name-only b1aa8b5 HEAD -- "*.h" "*.cpp")
checkStyle: 21 files, 0 violations
```

## Appendix: ring probe source

Built with `g++ -std=c++17 -O2 -Icommon/include ringNumbers.cpp build/common/liboutpost_common.a -lpthread -o ringNumbers`.

```cpp
// Throwaway probe: prints ring share and remap fractions using the project's ConsistentHashRing.
#include "common/consistentHash.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using outpost::common::ConsistentHashRing;
int main() {
    const int keys = 100000;
    ConsistentHashRing ring(150);
    for (int n = 1; n <= 3; ++n) ring.addNode("edge-" + std::to_string(n));
    std::map<std::string, int> share;
    for (int i = 0; i < keys; ++i) ++share[ring.getNode("key_" + std::to_string(i))];
    for (auto& [node, count] : share) std::printf("3 nodes, 100000 keys: %s owns %.1f%%\n", node.c_str(), 100.0 * count / keys);
    for (int existing = 3; existing <= 5; ++existing) {
        ConsistentHashRing r(150);
        for (int n = 1; n <= existing; ++n) r.addNode("edge-" + std::to_string(n));
        std::vector<std::string> before(keys);
        for (int i = 0; i < keys; ++i) before[i] = r.getNode("key_" + std::to_string(i));
        r.addNode("edge-" + std::to_string(existing + 1));
        int moved = 0;
        for (int i = 0; i < keys; ++i) moved += r.getNode("key_" + std::to_string(i)) != before[i];
        std::printf("add node %d->%d: %.1f%% of keys moved (ideal %.1f%%)\n", existing, existing + 1, 100.0 * moved / keys, 100.0 / (existing + 1));
    }
}
```
