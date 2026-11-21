# Phase J exit gate: standard HTTP caching and purge

Run: 2026-11-21 on `5506394`. Host: AMD Ryzen 5 5600H with Radeon Graphics, 12 threads, 7.4Gi RAM, WSL2 + Docker
Desktop 29.9.3; g++ 15.2.0. Owner decisions: scope "Core CDN set"; purge open on the router (trusted network);
dashboard layout "story on top", easier both to use and to change.

Commits:
- `ad75bba`: ETag helpers.
- `3fd1788`: storage and write-api answer 304; writer ids restricted to `[A-Za-z0-9._-]` because they are part of
  the ETag.
- `e19c5a4`: LRU lookups return the version, tag and age.
- `4993fb4`: the edge sends caching headers, answers 304, revalidates, honours no-cache, bounds stale serving and
  purges.
- `1597178`: the router passes the headers through and fans purges out.
- `5224b44`: integration checks.
- `6a06814`, `b22df00`, `58a477a`, `6e517d3`: dashboard.
- `5506394`: k6 conditional workload.

## 1. Tests in all three builds

New tests (each was red before its change: compile errors for the missing `httpCache.h` and LRU fields; 3 failing
write-api tests; 7 failing edge tests; 4 failing router tests):
- **common:**
  - `HttpCacheTest.EtagIsTheQuotedWriteIdentity`, `…IfNoneMatchMatchesExactListWildcardAndWeakTags`,
    `…IfNoneMatchDoesNotMatchOtherOrMissingTags`;
  - `LruCacheTest.LookupReportsVersionTagAndAge`.
- **write-api + storage** (real handlers):
  - `WriteApiConditionalTest.IfNoneMatchForTheStoredWriteIs304WithoutABody`,
    `…OtherIfNoneMatchReturnsTheValueAndItsEtag`;
  - `WriteApiValidationTest.WriterIdMustBeSafeInsideAnEtag`.
- **edge:**
  - `EdgeServerTest.CachedReadsCarryValidatorsAndFreshness`, `…MatchingIfNoneMatchIsAnswered304FromTheCache`,
    `…ExpiredEntryIsRevalidatedWithoutResendingTheValue`, `…NoCacheRequestRevalidatesAFreshEntry`;
  - `…StaleServingIsBoundedByStaleIfError`, `…PurgeEvictsTheKey`, `…WriteResponseCarriesTheWritesEtag`;
  - `…StatsMatchTheDocumentedSchema` now also checks `cache.staleIfErrorMs`, `revalidated`, `notModified` and
    `purges`.
- **router:**
  - `RouterFixture.ConditionalRequestHeadersAndCacheHeadersPassThrough`, `…NotModifiedPassesThroughWithoutABody`;
  - `…PurgeFansOutToEveryEdge`, `…PurgeReportsAnEdgeItCouldNotReach`.

Normal build:
```
$ ctest --test-dir build
1/5 Test #1: common_tests .....................   Passed    2.17 sec
2/5 Test #2: storage_tests ....................   Passed    0.10 sec
3/5 Test #3: write_api_tests ..................   Passed    0.57 sec
4/5 Test #4: edge_tests .......................   Passed    3.47 sec
5/5 Test #5: router_tests .....................   Passed    0.94 sec
100% tests passed, 0 tests failed out of 5
```

ASan + UBSan:
```
$ ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan
1/5 Test #1: common_tests .....................   Passed    4.33 sec
2/5 Test #2: storage_tests ....................   Passed    0.46 sec
3/5 Test #3: write_api_tests ..................   Passed    2.91 sec
4/5 Test #4: edge_tests .......................   Passed    5.42 sec
5/5 Test #5: router_tests .....................   Passed    3.91 sec
100% tests passed, 0 tests failed out of 5
sanitizer reports: 0
```

TSan:
```
$ TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan
1/5 Test #1: common_tests .....................   Passed    6.50 sec
2/5 Test #2: storage_tests ....................   Passed    0.50 sec
3/5 Test #3: write_api_tests ..................   Passed    2.75 sec
4/5 Test #4: edge_tests .......................   Passed    4.59 sec
5/5 Test #5: router_tests .....................   Passed    2.70 sec
100% tests passed, 0 tests failed out of 5
sanitizer reports: 0
```

163 tests in total.

## 2. End to end

`scripts/integrationTest.sh` → 21 PASS, 0 FAIL (6 new checks):
```
PASS  HIT carries ETag, Last-Modified and Age ("1795260907093776-edge-3")
PASS  Cache-Control has max-age and stale-if-error (max-age=60, stale-if-error=3600)
PASS  If-None-Match with the ETag is 304 without a body (304 0)
PASS  no-cache revalidates with the origin, which answers 304 (REVALIDATED)
PASS  purge reaches all 3 edges and evicts the key once (complete, edges, evicted: True 3 1)
PASS  read after purge is a MISS (MISS)
integrationTest: 0 failed check(s)
```

`scripts/dashboardAcceptance.sh` → 18 PASS, 0 FAIL. The new checks cover:
- the 8 scripts are served as JavaScript modules;
- static files are sent with `Cache-Control: no-cache`;
- storage stats are proxied while its keys are not;
- a 304 through the dashboard proxy;
- purge, then a MISS.

`dashboard/tests/browserClickTest.ps1` (headless Chrome on the Windows host):
- the guided demo ran all 9 steps, each passed, in 9.1 s;
- load generator: 200/200 HIT;
- request tester: PUT → WRITTEN, GET → HIT, Purge → PURGED, GET → MISS;
- the theme button cycles light → dark → auto, and the breaker filter shows only breaker events;
- no horizontal overflow at 1280 px or 390 px, in light or dark;
- 0 console errors.

The first run of that click test found a real bug that the curl-based checks cannot see. Every dashboard read was
being revalidated (step 4 got REVALIDATED instead of HIT). `fetch(…, {cache: 'no-store'})` adds
`Cache-Control: no-cache` to requests that have no `Cache-Control` header, and the edge now honours `no-cache`.
Fixed in `b22df00`. The same run showed that the load generator only ever read keys nobody had written (every
read a 404 MISS); fixed in `58a477a`.

## 3. k6 with conditional requests

`WORKLOAD=conditional scripts/runBenchmark.sh 60s 32`: 4096-byte values, 1000 keys, 32 clients, through the router.
Each client sends the ETag it last saw in `If-None-Match`, 5 % of reads send `Cache-Control: no-cache`, and the
rest of the requests are PUTs at the given share.

| Run | PUT share | req/s | conditional GETs → 304 | 304 p50 / p99 | 200 (ETag outdated) p50 / p99 | REVALIDATED p50 / p99 | body bytes 304 / 200 |
|---|---|---|---|---|---|---|---|
| `2026-11-21T113028Z-conditional` | 10 % | 5117 | 68659 / 261653 (26.2 %) | 3.26 / 13.42 ms | 3.46 / 13.64 ms | 5.79 / 17.49 ms | 0 / 4126 |
| `2026-11-21T113220Z-conditional` | 1 % | 7467 | 337295 / 435575 (77.4 %) | 2.94 / 9.74 ms | 3.14 / 10.04 ms | 5.01 / 12.30 ms | 0 / 4126 |

0 failed requests in both runs. no-cache reads were revalidated by a 304 from the origin 99.8 % and 100.0 % of the
time.

- **The 304 share depends on churn, not on the cache.** A conditional GET returns 304 only if nobody rewrote the key
  since that client last saw it. With PUT rate λ per key and a client revisiting a key every T seconds on average,
  the expected share is 1 / (1 + λT):
  - 10 %: λ ≈ 0.51/s and T ≈ 7.4 s, so ≈ 21 % expected; measured 26.2 %;
  - 1 %: λ ≈ 0.075/s and T ≈ 4.6 s, so ≈ 75 % expected; measured 77.4 %.
- **The saving is bandwidth, not latency.** On one machine a 304 is only about 0.2 ms faster than a 200 with 4 KiB.
  Each 304 skipped a 4126-byte body: in the 1 % run that is 337295 × 4126 B ≈ 1.39 GB not sent in 60 s (derived
  from the measured counts and mean body size). Over a real network that is transfer time and egress cost saved.
- **A revalidation costs an origin round trip**, but the origin sends no body (304): REVALIDATED p50 5.0–5.8 ms vs
  about 3 ms for an edge hit.
- **Writes per fsync were 2.73 in the 10 % run and 1.05 in the 1 % run.** Group commit only helps when writes
  overlap.

## 4. Stats schema, style and format

`EdgeServerTest.StatsMatchTheDocumentedSchema` passes with the new `docs/statsApi.md` rows.

```
$ python3 scripts/checkStyle.py
checkStyle: 47 files, 0 violations
$ scripts/format.sh --check
clang-format: 47 files, all formatted
```

## Limits, stated rather than hidden

- **Validators:**
  - `Last-Modified` comes from the write version (the writing edge's wall clock), to the second. Only ETags are
    used for conditional requests; `If-Modified-Since` is not implemented.
  - `Age` is the time since this edge stored or revalidated its copy. Without a shared clock there is no origin
    age.
- **Purge:**
  - Purge evicts one key. Tag- or prefix-based purge is a v2 idea.
  - If an edge cannot be reached, the router answers 502 with `complete: false`, and that edge may keep serving
    its copy for up to ttl + stale-if-error.
  - Purge is unauthenticated by owner decision (trusted network).
- **The dashboard** sends `Cache-Control: no-transform` on plain requests so that `fetch` does not add
  `no-cache` (see §2).

## Result

Gate J passed.
