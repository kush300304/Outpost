# Hands-on tour: test Outpost yourself, step by step

This tour takes you through the whole project in order: what Outpost is for, what each piece does, and how to
check with your own eyes that it does it. Every step has the same shape:

- **Why:** the motive, the problem this piece solves.
- **Do:** commands to run in a terminal (Linux, macOS, or WSL on Windows), from the repository root.
- **You should see:** what a pass looks like.
- **What it proves:** the guarantee behind it.
- **Check yourself:** one question to answer out loud before moving on.

Plan about two hours for the whole tour. Steps 0–2 need no Docker; from step 3 on, Docker Desktop must be
running.

---

## Step 0. The big picture: what Outpost is and what "working" means

**The setting.** Many servers ("edges") answer reads close to users; one source of truth (the "origin") owns
the data. That is how CDNs and large web services are built. Outpost is a small, complete version of it.

**The two problems it solves.**
1. **The origin fails, and everything fails with it.** If every edge keeps calling a dead or slow origin, users
   get errors and the retries make the origin's recovery harder (a "retry storm").
2. **Adding or removing a server reshuffles everything.** With `hash(key) % N`, changing N remaps almost every
   key, so every cache goes cold at once.

**The guarantees the project promises.** Every later step checks one of these:

| # | Guarantee | Main mechanism |
|---|---|---|
| G1 | Reads of cached data keep working while the origin is down | Circuit breaker + serving stale copies |
| G2 | No acknowledged write is ever lost or applied twice | Write-ahead logs + versioned writes |
| G3 | Losing or adding an edge moves only that edge's share of keys | Consistent hashing |
| G4 | One client cannot flood an edge | Per-client token bucket |
| G5 | Standard HTTP caching: clients and caches can revalidate instead of re-downloading | ETag, 304, Cache-Control, purge |
| G6 | Fast: cache hits under 10 ms, durable writes without one disk flush each | In-memory cache, group commit |

**The pieces.**

```
client ──► router :8080 ──► edge-1 / edge-2 / edge-3 ──► write-api ──► storage
                             (cache, rate limit,          (checks      (the data,
                              breaker, write queue)        requests)    on disk)
browser ──► dashboard :8090 (a live view of all of it)
```

"Origin" means write-api plus storage together.

**Check yourself:** Which guarantee would break if the edges called the origin directly without a breaker?

---

## Step 1. The code map

**Why:** knowing where things live makes every later step concrete.

**Do:**
```bash
ls common/include/common/ router/src edge/src write-api/src storage/src dashboard/src scripts
```

**You should see:**
- `common/`: the five hand-built building blocks:
  - `consistentHash` (G3), `lruCache` (G1, G6), `tokenBucket` (G4), `circuitBreaker` (G1), `wal` (G2, G6);
  - plus small helpers such as `httpCache` (G5) and `urlEncode`.
  - Their tests are in `common/tests/`.
- One folder per service, each with its own `include/`, `src/` and `tests/`.
- `dashboard/`: the web page.
- `scripts/`: the automated test suites.
- `docs/`: the explanations. `docs/designWalkthrough.md` explains every module.

**Check yourself:** Which folder would you open to see how a write is made durable?

---

## Step 2. Build it and run the unit tests

**Why:** unit tests prove each building block and each service is correct on its own, fast, without Docker. They
run in seconds, so every change was tested first.

**Do:**
```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```
(The first configure downloads the libraries, about 2 minutes.)

**You should see:** `100% tests passed, 0 tests failed out of 5`. Five suites, 163 tests in total.

**Look inside one test.** The WAL must survive a crash in the middle of writing a record (a "torn tail"):
```bash
grep -n "TEST(WalTest" common/tests/testWal.cpp | head -20
./build/common/common_tests --gtest_filter='WalTest.TornTail*'
```
That test writes records, chops bytes off the end of the file (what a power cut does), reopens the log, and
checks that every complete record survived and new writes still work.

**And the group-commit test:**
```bash
./build/common/common_tests --gtest_filter='WalTest.ConcurrentAppendsShareFsyncs'
```
8 threads write 400 records with fsync on; the test passes only if fewer than 400 fsyncs were needed.

**What it proves:** the building blocks behind G2, G3, G4 and G6 do what they claim, including edge cases that
are hard to cause by hand.

**Check yourself:** Why is it important that the torn-tail test chops bytes *off the end*, not out of the middle?

---

## Step 3. Start the cluster and look around

**Why:** from here on you test the real system: seven containers talking over a network, as in production.

**Do:**
```bash
docker compose up -d --build --wait
docker compose ps --format "table {{.Service}}\t{{.Status}}\t{{.Ports}}"
curl -s localhost:8080/health; echo
```

**You should see:**
- 7 services `healthy`: router, edge-1..3, write-api, storage, dashboard.
- Only the router has `0.0.0.0:8080->8080` and the dashboard `0.0.0.0:8090->8090`. The others have no host
  port: clients cannot bypass the router, and nobody outside can reach storage.
- `{"edgesConfigured":3,"edgesInRing":3,"service":"router","status":"healthy"}`: the router sees all three
  edges.

**Where the logs are.** Each service prints its log to standard output, and Docker stores it. There is no
logs folder.
```bash
docker compose logs --tail 5 storage
docker compose logs -f edge-1        # live; press Ctrl+C to stop
```
storage's start-up line says how many records it recovered from its write-ahead log.

**Check yourself:** Why are the edges, write-api and storage not published to the host?

---

## Step 4. Your first requests: MISS, HIT and what the headers mean

**Why:** this is the normal path, the one 99 % of traffic takes.

**Do:**
```bash
curl -i localhost:8080/key/tour-1                      # never written
curl -i -X PUT localhost:8080/key/tour-1 -d hello
curl -i localhost:8080/key/tour-1
```

**You should see:**
1. `404` with `X-Cache: MISS`: the edge had no copy, asked the origin, and storage has no such key.
2. `200` with `X-Write-Mode: DIRECT`, `X-Node: edge-N` and an `ETag`:
   - the edge wrote through to storage, which put it on disk before answering;
   - the edge also cached the value.
3. `200` with `X-Cache: HIT` from the same `X-Node`: answered from the edge's memory, so the origin was not
   contacted. The headers mean:
   - `ETag: "<version>-<edge>"`: which write produced this value;
   - `Age`: seconds this edge has held it;
   - `Cache-Control: max-age=…, stale-if-error=3600`: how long it stays fresh, and how long it may be served
     stale if the origin fails;
   - `Last-Modified`: when it was written.

**What it proves:** the read and write paths work end to end. A hit never touches the origin.

**Check yourself:** Why did the third request come from the same edge as the second?

---

## Step 5. Routing: consistent hashing and failover (G3)

**Why:** every key must have one owner edge, so its cached copy lives in one place. When an edge dies, only its
keys should move.

**Do:**
```bash
curl -s localhost:8080/debug/route/tour-1; echo        # which edge owns tour-1
for k in a b c d e f g h i j; do curl -s localhost:8080/debug/route/$k; echo; done
curl -s localhost:8080/debug/stats | python3 -m json.tool | head -30
```

**You should see:** each key always names the same edge; ten keys spread over all three; `routed` counts per
edge roughly equal over many requests.

**Now kill an edge:**
```bash
docker compose kill edge-2
sleep 3                                                 # the router notices within about 2 s
for k in a b c d e f g h i j; do curl -s localhost:8080/debug/route/$k; echo; done
curl -s -o /dev/null -w '%{http_code}\n' localhost:8080/key/tour-1
docker compose start edge-2
sleep 3; curl -s localhost:8080/health; echo
```

**You should see:**
- After the kill, keys that belonged to edge-2 now name edge-1 or edge-3. **Keys owned by the others did not
  change.**
- Requests still succeed.
- After the restart, `edgesInRing` is back to 3, and edge-2 gets its own keys back.

**What it proves:** G3. With `% N` hashing, almost every key would have moved.

**Check yourself:** What are "virtual nodes", and why does the router use 150 per edge?

---

## Step 6. HTTP caching: 304, revalidation and purge (G5)

**Why:** clients and other caches should be able to ask "has this changed?" and get a cheap "no" instead of the
whole value again. Operators need to remove a key from every cache right away.

**Do:**
```bash
ETAG=$(curl -s -D - -o /dev/null localhost:8080/key/tour-1 | tr -d '\r' | awk 'tolower($1)=="etag:"{print $2}')
echo "$ETAG"
curl -i -H "If-None-Match: $ETAG" localhost:8080/key/tour-1           # conditional read
curl -s -D - -o /dev/null -H 'Cache-Control: no-cache' localhost:8080/key/tour-1 | grep -i x-cache
curl -s -X POST localhost:8080/purge/key/tour-1 | python3 -m json.tool
curl -s -D - -o /dev/null localhost:8080/key/tour-1 | grep -i x-cache
```

**You should see:**
1. `304 Not Modified` with **no body**: your copy is current.
2. `X-Cache: REVALIDATED`: the edge sent its ETag to the origin, and storage answered 304, so the value was not
   re-sent.
3. Purge: `"complete": true` and one answer per edge, with `"purged": true` on the edge that had it.
4. `X-Cache: MISS`: the cache is empty, so the next read went to the origin.

**What it proves:** G5. A 304 carries 0 bytes instead of the value: the benchmark measured 77 % of conditional
reads answered that way at 1 % writes.

**Check yourself:** What exactly is in our ETag, and why does that make "is my copy current?" cheap to answer?

---

## Step 7. Rate limiting (G4)

**Why:** one misbehaving client must not use up an edge's capacity for everyone else.

**Do:** 400 requests at once from one pretend client, then one from another client:
```bash
seq 400 | xargs -P 40 -I{} curl -s -o /dev/null -w '%{http_code}\n' -H 'X-Forwarded-For: 198.51.100.7' \
    localhost:8080/key/tour-hot | sort | uniq -c
curl -s -o /dev/null -w '%{http_code}\n' -H 'X-Forwarded-For: 198.51.100.8' localhost:8080/key/tour-hot
```

**You should see:** some `429` (Too Many Requests) among the first client's answers. The second client gets a
normal answer (here `404`, the key was never written).

**What it proves:** G4. Each client has its own token bucket: a burst of 200, refilled at 100 per second, per
edge.

**Check yourself:** Why does the limiter key on `X-Forwarded-For`, and what is the security caveat?

---

## Step 8. The origin fails, and recovers (G1, G2)

**Why:** this is the reason the project exists. Watch the breaker open, reads keep working, writes get queued,
and everything heal by itself.

**Do:** prepare one cached key, then stop storage for real:
```bash
curl -s -o /dev/null -X PUT localhost:8080/key/tour-2 -d before
docker compose stop storage
sleep 8                                                 # 3 failed health checks, about 2 s apart
docker compose logs --since 30s edge-1 edge-2 edge-3 | grep -iE "breaker|health check"
curl -i localhost:8080/key/tour-2                       # cached key
curl -i localhost:8080/key/never-cached                 # uncached key
curl -i -X PUT localhost:8080/key/tour-3 -d during-outage
```

**You should see:**
- **The logs:** `Health check failed (consecutive: 1, 2, 3)`, then `Circuit breaker CLOSED -> OPEN` on every
  edge.
- **The cached key:** `200` with `X-Cache: STALE`. It is still served (G1).
- **The uncached key:** `503`. The edge can't serve what it doesn't have, and it doesn't hammer a dead origin.
- **The write:** `202` with `X-Write-Mode: QUEUED`. It is on disk in that edge's own write-ahead log.

**Recover:**
```bash
docker compose start storage
sleep 8
docker compose logs --since 15s edge-1 edge-2 edge-3 | grep -iE "breaker|reconcil"
docker compose exec -T storage curl -s localhost:9091/key/tour-3; echo
```

**You should see:**
- **The logs:** `OPEN -> HALF_OPEN` (exactly one probe), then `HALF_OPEN -> CLOSED`, then
  `Reconciling 1 queued writes` and `Reconciliation finished: 1 delivered`.
- **Storage** now holds `during-outage`, with the version stamped when the edge accepted it.

**What it proves:**
- **G1:** reads kept working.
- **G2:** the write accepted during the outage was delivered.
- **Gentle recovery:** the origin saw one probe, not a stampede.

**Check yourself:** If the edge had crashed in the middle of replaying its queue, why would no write be applied
twice?

---

## Step 9. Durability: kill storage hard (G2, G6)

**Why:** "acknowledged" must mean "on disk". A hard kill (no chance to clean up) is the honest test.

**Do:**
```bash
curl -s -o /dev/null -w '%{http_code}\n' -X PUT localhost:8080/key/tour-4 -d survives
docker compose kill storage                     # SIGKILL: no shutdown code runs
docker compose start storage
sleep 10                                        # storage replays its log; the edges' breakers close again
docker compose logs --tail 2 storage
docker compose exec -T storage curl -s localhost:9091/key/tour-4; echo
```

**You should see:**
- **The write:** `200`.
- **After the kill and restart:** storage's log says `WAL … recovered: N records, M keys, 0 damaged bytes
  skipped, 0 tail bytes truncated`, and `tour-4` is still `survives`, with the version and writer of the
  original write.

**Now see group commit.** Send 200 writes at once (20 in parallel) and read storage's counters. They count from
storage's start, so they include only these writes:
```bash
seq 200 | xargs -P 20 -I{} curl -s -o /dev/null -X PUT localhost:8080/key/gc-{} -d x
curl -s localhost:8090/storage/debug/stats | python3 -m json.tool | grep -A3 '"wal"'
```
You should see `"appends": 200` and far fewer `fsyncs` (71 in the dry run). Every write was on disk before its
`200`, but writes that arrived together shared one disk flush (G6).

**What it proves:** G2 at the disk level. The chaos test (step 11) does this hundreds of times under load.

**Check yourself:** Why must a failed fsync stop the log instead of being retried?

---

## Step 10. The dashboard

**Why:** to see the whole story at once, and to show it to someone in 10 seconds.

**Do:** open **http://localhost:8090** and click **Run all steps**.

**You should see:**
- **The guided demo:** the nine steps turn green one by one in about 10 seconds: write, purge, MISS, HIT, 304,
  breakers OPEN, STALE, QUEUED, recovery confirmed by REVALIDATED.
- **The flow diagram** lights up each request's path.
- **The edge cards** show the breakers opening and the queue rising and draining.
- **The event log** records it all.

Then try the tools:
- send your own GET / PUT / Purge (tick *If-None-Match* or *no-cache*);
- flip the outage switch;
- run the load generator;
- hover any dotted label for an explanation.

**What it proves:** the same guarantees, checked live by the page itself. Each step passes only if the real
cluster did it.

**Check yourself:** Step 9 of the demo expects `REVALIDATED`. Why does that prove the queued write reached
storage?

---

## Step 11. The automated suites: the same checks, at scale

**Why:** a person can check a few requests; scripts check thousands, the same way every time.

**Do** (each takes from 20 seconds to 3 minutes):
```bash
scripts/integrationTest.sh          # 21 checks: routing, caching, purge, rate limit, ports
scripts/dashboardAcceptance.sh      # 18 checks through the dashboard's proxy
scripts/shutdownTest.sh             # every service stops cleanly on SIGTERM
scripts/chaosTest.sh                # kills storage, write-api and an edge under load
scripts/runBenchmark.sh 60s 32      # k6 load test; results saved under benchmarks/results/
```

**You should see:**
- `0 failed check(s)` from the integration and acceptance tests;
- `PASS` ×4 from the shutdown test;
- `RESULT: PASS` from the chaos test, with *acknowledged writes persisted = sent*;
- a benchmark summary with p50/p95/p99 per request type.

**How to read the chaos result:** it sends unique writes and reads warm keys continuously while it:
1. kills storage, then write-api;
2. kills an edge **200 ms into replaying its queue**.

It passes only if every write that got 200/202 is in storage with its exact value (G2), and no warm read failed
during the outages (G1). The `duplicates` count shows replays recognised and not applied twice.

**Check yourself:** Why does the benchmark have a fixed-rate mode (`RATE=`), and what went wrong without it?

---

## Step 12. Sanitizers and CI

**Why:** some bugs, such as data races and memory errors, don't show up in normal runs. Sanitizers make them
visible; CI makes sure nobody forgets to run anything.

**Do** (optional, about 5 minutes each):
```bash
cmake -S . -B build-tsan -DOUTPOST_SANITIZE=thread && cmake --build build-tsan -j && ctest --test-dir build-tsan
gh run list --branch main --limit 3
```

**You should see:** 100 % passed with no `ThreadSanitizer` warnings, and green CI runs.

**What it proves:** the concurrency (group commit, the breaker, the drain) is free of data races in every test.

---

## Wrap-up: every guarantee and its evidence

| Guarantee | You saw it in | Automated proof |
|---|---|---|
| G1 reads survive an origin outage | Step 8, dashboard steps 6–7 | Chaos test: 0 failed warm reads during outages |
| G2 no write lost or applied twice | Steps 8–9 | Chaos test: acknowledged = persisted; replay duplicates recognised |
| G3 only a dead edge's keys move | Step 5 | Router tests; hash-ring distribution tests |
| G4 per-client rate limit | Step 7 | Integration test (429 + Retry-After) |
| G5 standard HTTP caching | Step 6, dashboard steps 2–5 | Integration test; conditional benchmark (77 % 304s) |
| G6 fast and durable | Step 9 | Benchmarks: hits 0.98 ms p50; PUT 113.9 → 7.9 ms with group commit |

If you can answer every "check yourself" question, you can explain Outpost in an interview. The reasoning
behind every answer is in `docs/designWalkthrough.md`.
