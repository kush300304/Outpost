# Outpost: a design walkthrough

This is the long-form explanation of how Outpost works and why it is built the way it is: every module,
every major decision, and the bugs that measurements uncovered along the way. The README is the short
version. All numbers come from real runs recorded in `docs/gates/`, `docs/finalCheck.md` and
`benchmarks/results/`, on an AMD Ryzen 5 5600H laptop (12 threads, WSL2, Docker Desktop).

Contents:
1. [The problems](#1-the-problems)
2. [The architecture in plain words](#2-the-architecture-in-plain-words)
3. [The five building blocks](#3-the-five-building-blocks-common)
4. [The services](#4-the-services)
5. [Request lifecycles, with real timings](#5-request-lifecycles-with-real-timings)
6. [Engineering details that mattered](#6-engineering-details-that-mattered)
7. [How we know it works](#7-how-we-know-it-works)
8. [Bugs the measurements found](#8-bugs-the-measurements-found)
9. [Trade-offs, limitations and next steps](#9-trade-offs-limitations-and-next-steps)
10. [Glossary](#10-glossary)

---

## 1. The problems

Many systems serve lots of reads from servers close to users (edges) and send writes to one source of truth
(the origin). They tend to fail in predictable ways:

| Problem | What goes wrong | Outpost's answer |
|---|---|---|
| The origin goes down | Every request fails; the whole service is down | Edges serve the copies they have (marked `STALE`) and queue writes durably until the origin is back |
| A struggling origin gets hammered | Every edge retries, so "slow" becomes "dead" (a retry storm) | A **circuit breaker** stops calling the origin and lets one test request through later |
| Adding or removing a server reshuffles everything | With `hash(key) % N`, adding one server moves nearly every key, so every cache goes cold | **Consistent hashing with virtual nodes**: only about 1/N of the keys move |
| One client floods the system | Everyone else slows down | A **token bucket per client** at each edge |
| Replays after a crash corrupt data | A write applied twice, or an old write replayed over a newer one | **Versioned writes**: storage applies a write only if it is newer |

The deliberate choice behind all of this is **availability over strict consistency** during a failure:
serve something slightly old rather than nothing. That is the AP side of the CAP trade-off, and BASE rather
than ACID.

---

## 2. The architecture in plain words

```
client ──► router ──► one of 3 edges ──► write-api ──► storage
                          │
         cache · rate limiter · circuit breaker · write queue (WAL)
```

A library analogy helps:

| Piece | Analogy | What it really is |
|---|---|---|
| **storage** | The main archive: the only place the real books live | The source of truth, a versioned key-value store made durable by a write-ahead log |
| **write-api** | The archive's front desk, which checks every request | Validation in front of storage; the only service that talks to storage |
| **edge-1/2/3** | Branch libraries with copies of popular books | Caches that also protect the origin |
| **router** | The receptionist who says "for that book, go to branch 2" | Picks the edge that owns each key |

**The origin** is write-api and storage together. The spec splits the origin into two independently
deployable services.

In CDN terms:
- the edges are edge servers;
- write-api plus storage are the customer's origin;
- the router stands in for the CDN's mapping system.

**Ports.**
- The router (8080) and the dashboard (8090) are the only ports published to the host.
- The edges (8081–8083), write-api (9090) and storage (9091) are reachable only inside the Docker network,
  so clients cannot bypass the edges and hit storage directly.

**Libraries.** The services use cpp-httplib (HTTP), nlohmann/json and spdlog. The five core building blocks
are hand-written with no third-party code. That is a project rule, so each one can be explained on a
whiteboard.

---

## 3. The five building blocks (`common/`)

### 3.1 Consistent hash ring: "which edge owns this key?" (`consistentHash.*`)

**Idea.**
- Put every hash value on a circle of 2⁶⁴ positions.
- Each edge gets **150 points** on the circle: `hash("edge-1#0")`, `hash("edge-1#1")`, and so on. These are
  its virtual nodes.
- A key belongs to the first point clockwise from `hash(key)`.

**Why it limits remapping.** A new edge takes over only the arcs just before its own points. Every other key
still meets the same first point clockwise, so adding an edge to N edges moves about 1/(N+1) of the keys.

**Why 150 virtual nodes.** With one point per edge, the arcs between points are random, and one edge can own
half the circle. Many points per edge average the arcs out. When an edge leaves, its keys spread over all the
remaining edges, not just one neighbour.

**Implementation.**
- The ring is a `std::set<std::pair<uint64_t, std::string>>` of `(hash, nodeId)` pairs.
- `getNode` does `lower_bound({hash(key), ""})` and wraps to `begin()` past the end, so a lookup is
  O(log(N·V)).
- Storing pairs instead of a `map<hash, node>` means two colliding points coexist instead of overwriting
  each other. Removing a node erases only its own points, and ownership does not depend on the order nodes
  were added. That matters because a restarted router must route exactly like the old one.

**The hash bug.** The original hash was plain FNV-1a.
- **Symptom:** across three nodes it gave a **10.4 / 55.6 / 34.0 %** split.
- **Cause:** for short, similar strings like `edge-1#0` and `edge-1#1`, FNV-1a's high bits barely change.
  A sorted ring orders by the high bits, so the virtual nodes formed clumps; 9 of 16 top-nibble buckets were
  empty.
- **Fix:** passing the FNV-1a result through MurmurHash3's `fmix64` finalizer (shift, xor, multiply) spreads
  every input bit over the whole word.
- **Result:** **32.0 / 36.2 / 31.9 %**. Adding a node moves 29.3 / 23.0 / 16.4 % of keys, against an ideal
  of 25 / 20 / 16.7 %.

### 3.2 LRU cache: "keep the popular data close" (`lruCache.*`)

**Classic structure.**
- A doubly-linked list holds the entries in recency order, most recent at the front.
- A hash map points from each key to its list node.
- Lookup, insert and evict are O(1). Every access `splice`s the node to the front, and eviction drops the
  back.

**Freshness (TTL, 60 s by default).**
- Each entry records when it was stored. `get()` returns only fresh entries; `lookup()` returns any entry
  with a `fresh` flag.
- Expired entries are **kept** on purpose. When the origin cannot answer, an expired copy is served as
  `X-Cache: STALE`; when the origin is healthy, it is fetched again. In HTTP terms this is the
  *stale-if-error* pattern (RFC 5861).
- The TTL also bounds staleness, which is what makes the system eventually consistent: without it, an edge
  could serve an old value forever.

**Versioned puts.**
- `put(key, value, version)` is ignored when the cached entry is newer.
- That closes the classic cache-aside race: a read misses and fetches v1, a write stores v2 and caches it,
  then the slow read finishes and would otherwise put v1 back.

### 3.3 Token bucket: "one client cannot flood us" (`tokenBucket.*`)

**How it works.**
- Each client has a bucket of up to **200 tokens** (the burst), refilled at **100 tokens per second** (the
  sustained rate).
- Each request takes one token. An empty bucket answers `429 Too Many Requests` with `Retry-After: 1`.

**Lazy refill.** There is no timer thread. Every `tryConsume` adds `elapsed × rate` tokens, clamped to the
capacity, so thousands of idle buckets cost nothing.

**Use at the edge.**
- There is one bucket per client per edge, keyed by the first `X-Forwarded-For` hop, which the router sets.
  Otherwise every request would appear to come from the router.
- Trusting that header is only acceptable because the network is trusted; authentication is out of scope.
- The bucket table is bounded: idle buckets (60 s) are evicted first, then the least recently seen one.
- Buckets are held as `shared_ptr`, so evicting one while another thread uses it is safe.

**Why a token bucket rather than a fixed window.** A token bucket allows short bursts but caps the average.
A fixed window lets a client fire two full windows back to back around the boundary.

### 3.4 Circuit breaker: "stop knocking on a dead door" (`circuitBreaker.*`)

```
CLOSED ──3 consecutive failures──► OPEN ──5 s elapsed──► HALF_OPEN
   ▲                                 ▲                       │
   └────────── probe succeeds ───────┼──── probe fails ──────┘
```

**`isAllowed()`.**
- CLOSED: always yes.
- OPEN: no until the reset timeout elapses. The first call after that moves to HALF_OPEN and returns yes
  once; that call is the probe.
- HALF_OPEN: no while the probe is outstanding.

**Three bugs fixed, each with a test.**
1. **Off-by-one.** It admitted **two** half-open probes.
2. **Wedge.** A probe whose result was never recorded (an exception, a dropped request) left the breaker
   HALF_OPEN forever. Now a probe outstanding for the reset timeout counts as lost, and one new probe is
   admitted.
3. **Deadlock.** The "state changed" callback ran under the breaker's mutex, so any callback that asked for
   the state deadlocked. Now each method records the transition under the lock and calls the callback after
   unlocking.

**Who calls `isAllowed()`.** Only the edge's heartbeat thread, which health-checks write-api:
- every second while CLOSED;
- never while OPEN;
- exactly once (the probe) in HALF_OPEN.

Request handlers only read `state()` and report successes and failures. If they called `isAllowed()`, a
random user request could become the probe, or several could slip through. Failed requests still count
toward the threshold.

**Why 3 failures, 1 s heartbeat, 5 s reset.**
- An outage is detected in seconds (recorded runs: 0.7–5.3 s, depending on how the origin dies).
- During recovery the origin sees one probe, not a thundering herd.
- Lower thresholds react faster but flap on one-off errors; higher ones serve errors for longer.

### 3.5 Write-ahead log: "never lose a write" (`wal.*`)

Storage uses it for durability, and each edge uses one as its outage queue.

**Record layout.**
```
[magic 4][crc32 4][payloadLength 4][ seq 8 | op 1 | version 8 | writerId | key | value ]
```
- The CRC covers the length and the payload. An earlier version covered only the payload, so one flipped
  bit in a length made the reader consume the wrong number of bytes and lose everything after it.
- The magic number gives recovery something to resynchronise on.

**Sequential I/O.**
- The file is opened in append mode, and each record is one unbuffered `fwrite` at the end, followed by
  `fsync`.
- Writes and replays are therefore sequential, the same shape as database WALs, Kafka's log or Redis's
  append-only file.
- The cost of durability is not seeking but the `fsync`: the call waits until the data is on stable storage.

**Recovery on open.**
- Every valid record is kept.
- A damaged record in the middle is skipped by scanning forward to the next magic number with a valid CRC.
- An invalid tail (a torn final write after a crash) is cut off. Without this, new records are appended
  after the torn bytes and every later acknowledged write becomes unreadable. An early test proved it:
  three acknowledged writes, all lost.

**Failed appends** truncate the file back to its previous size, so a half-written record never remains.

**`discardThrough(seq)`.**
- After draining its queue, an edge removes exactly the prefix it processed.
- The WAL writes the remaining records to a temporary file, fsyncs it, renames it over the original (rename
  is atomic) and fsyncs the directory.
- The old code truncated the whole file and wiped any write queued during the drain.
- Sequence numbers keep increasing across truncation; the old code reset them.

**Start-up.** `takeRecoveredEntries()` hands storage the records the recovery scan already parsed, instead
of reading the file a second time. Start-up went from 7.2 s to 3.7 s on a 46.7 MB log (Debug build).

**The price of durability.**
- Writes take about **114 ms** at p50 under 32 concurrent writers with fsync on, and **6 ms** with it off.
- Storage applies writes one at a time, each holding the lock through its fsync, so concurrent writers queue.
- The standard fix is **group commit**: append a batch of writes (still sequentially), then do one fsync for
  the batch.

### 3.6 Small helpers

- **`urlEncode`.** RFC 3986 percent-encoding of keys on every hop. Without it, the key `a?b` reached storage
  as `a`, because `?` starts a query string. It is hand-written because cpp-httplib's encoder is in its
  internal `detail` namespace.
- **`requestLimits`.** The 256-byte key and 1 MiB value limits, shared by the edge and write-api. The edge
  rejects an oversized write before queueing it, so a queued write can never be rejected later.
- **`shutdownSignal`.**
  - Blocks SIGINT/SIGTERM before any thread starts and waits for them on a dedicated `sigwait` thread, which
    then stops the server so `main` returns normally.
  - Inside a container the service is PID 1, which ignores SIGTERM unless it handles it. `docker stop` used
    to wait 10 s and then kill the process; now a stop takes 0.33 s.

---

## 4. The services

### 4.1 storage: the source of truth (`storage/`)

**Versioned values.**
- `KvStore` keeps, for every key, the value and the `(version, writerId)` of the write that produced it.
- A put compares the incoming pair with the stored pair:
  - newer → **applied**: logged to the WAL, then stored;
  - identical → **duplicate**: nothing changes;
  - older → **superseded**: nothing changes.
- Only applied writes are appended to the WAL. On start-up the WAL is replayed with the same rule.

**Why idempotency lives here.** One durable comparison gives two guarantees:
- replaying a write that already landed is harmless;
- an old queued write can never overwrite newer data.

A separate idempotency-key store in write-api was considered, but it would live in memory and be lost on
restart. The versions are already durable in storage's WAL.

**Trade-offs.**
- Versions are microsecond timestamps from the edge's clock, made strictly increasing per edge, with the
  edge id as tie-breaker. Two edges on different machines would rely on NTP for ordering; a hybrid logical
  clock is the v2 fix.
- Deletes are not in v1: an unversioned delete followed by a replayed older write would resurrect the key,
  so deletes need versioned tombstones.

**API.**
- `GET /key/:k` → value, version, writerId, or 404.
- `PUT /key/:k {value, version, writerId}` → `{result: applied | duplicate | superseded}`.
- Plus `/health` and `/debug/stats`, which include the WAL recovery report.

### 4.2 write-api: the guarded front door (`write-api/`)

**Validation.** Key ≤ 256 bytes, value ≤ 1 MiB, version > 0, non-empty writer id; otherwise 400.

**Health reflects storage.** `/health` is 200 only if storage answers its own `/health` within 1 s.
Originally it was always 200, so a storage-only outage never opened any breaker; edges kept calling and
answering 404.

**Error mapping.**
- Storage absent → 404.
- Storage unreachable or failing → **502**, never 404. Treating "the database is down" as "the key does not
  exist" would make clients believe data is gone.

**Simulated outage** (only with `ADMIN_ENABLED=1`).
- `POST /admin/outage {"enabled":true}` makes `/health` and every `/key` request answer 503.
- Edges then react exactly as to a real outage, so the dashboard demo exercises the real code paths.

### 4.3 edge: cache, rate limiter, breaker and queue (`edge/`)

**Read path** (`GET /key/:k`):

| Cache state | Breaker | What happens |
|---|---|---|
| Fresh entry | CLOSED | `200`, `X-Cache: HIT` (origin untouched) |
| Missing or expired | CLOSED | Fetch from write-api; `200 MISS` and cache with its version, or `404 MISS` |
| Missing or expired | CLOSED, fetch fails | Expired entry served as `200 STALE`; nothing cached → `503` |
| Any entry | OPEN / HALF_OPEN | `200 STALE` |
| Nothing | OPEN / HALF_OPEN | `503` (cannot serve what it does not have) |

A rate-limited request gets `429` before any of this.

**Write path** (`PUT /key/:k`).
1. Validate the size: oversized → 400, even while degraded.
2. Stamp a version: the current time in microseconds, made strictly increasing with a compare-and-swap loop
   on `lastVersion_`.
3. If the breaker is CLOSED, forward the write. A 200 is passed through as `X-Write-Mode: DIRECT`. A 4xx is
   passed through unchanged; a validation error must not be queued.
4. Otherwise, or if forwarding fails, append the write with its version to the edge's WAL and answer
   `202 X-Write-Mode: QUEUED`.
5. In both success cases, cache the value with its version. Reads during an outage then see the client's own
   write.

**Maintenance thread** (every second):
- **Heartbeat:** if `breaker.isAllowed()`, call write-api `/health` and record the result.
- **Drain:** if the breaker is CLOSED and the queue is not empty (including right after start-up):
  1. Take a snapshot of the queue.
  2. Send each write in order with its **original** version, stopping at the first origin failure to keep
     the order.
  3. Count a 4xx as rejected and drop it. The edge's own size checks make that unreachable for writes it
     accepted.
  4. `discardThrough` the last resolved sequence number.

**Observability.**
- `/debug/stats` reports: cache hit rate and size, breaker state with the last 10 transitions, the caller's
  remaining tokens, queue depth and counters, and the last 100 events (breaker changes, reconcile start and
  finish, rate-limit rejections at most once a second).
- The schema is in `docs/statsApi.md`. A test reads that document's field table and checks every field
  against a live edge, so the dashboard cannot silently break.

### 4.4 router: the stateless entry point (`router/`)

**Routing.**
- Hashes the key onto the ring of reachable edges and forwards GET or PUT to the owner.
- The key is percent-encoded again, the client address is appended to `X-Forwarded-For`, and the body is
  passed through.
- The edge's status, body and `X-Cache`, `X-Node`, `X-Write-Mode` and `Retry-After` headers come back
  unchanged.

**Membership.**
- Every configured edge (`EDGE_NODES`) starts on the ring. A health thread checks each edge every second;
  two consecutive failures remove it, and one success adds it back. Only that edge's keys move.
- If the owner refuses a connection, the request path removes it at once and retries once on the new owner,
  so requests do not fail while waiting for the next health check.
- Retrying a PUT on a different edge can store the same value twice under two versions, which is harmless
  under last-writer-wins.

**Stateless.** The router persists nothing. A restarted router derives the same ring from its configuration,
because ownership does not depend on insertion order.

**Adding an edge.** Adding a fourth edge means a new `EDGE_NODES` entry and a router restart; about a quarter
of the keys move.

**Debugging.** `/debug/route/:k` names a key's owner, and `/debug/stats` lists each edge's health, ring
membership and routed count.

---

## 5. Request lifecycles, with real timings

**Normal read and write** (final check, fresh clone):
```
GET  /key/foo       → 404, X-Cache: MISS, X-Node: edge-3    (the edge asked the origin)
PUT  /key/foo bar   → 200, X-Write-Mode: DIRECT, applied
GET  /key/foo       → 200, X-Cache: HIT                       (origin untouched)
```
Measured: a cache hit through the router takes **0.96 ms p50 / 1.64 ms p99** at 2345 requests/s.

**Outage** (`docker compose stop storage`):
1. write-api's `/health` turns 503. The edge's heartbeats fail, and after three of them the breaker opens:
   **5.3 s** in this run; other recorded runs took 0.7–4.4 s (`docs/gates/`).
2. `GET /key/foo` → `200 STALE` with the cached value.
3. `PUT /key/foo during-outage` → `202 QUEUED`; the write is in the edge's WAL, durable before the response.

**Recovery** (`docker compose start storage`):
1. After the 5 s reset, the breaker admits one probe. The probe succeeds: `OPEN → HALF_OPEN → CLOSED`
   (**4.5 s** after storage was back in this run).
2. Edge log: `Reconciling 1 queued writes` … `Reconciliation finished: 1 delivered`.
3. Storage now holds `during-outage` with the version stamped when the edge accepted it.

**An edge dies.** The router removes it on the first refused connection (the first request for one of its
keys waited **1.0 s**, the bounded DNS lookup), or after two failed health checks (**1.8 s**). Its keys are
then served by the other edges.

---

## 6. Engineering details that mattered

### HTTP server tuning (cpp-httplib defaults that hurt)

The benchmark and the concurrency tests found three defaults that hurt, each now set once for the whole
build in the top-level `CMakeLists.txt`:

| Setting | Default problem | Found by | Fix |
|---|---|---|---|
| `TCP_NODELAY` | Off, so Nagle's algorithm plus delayed ACKs made every keep-alive request after the first wait ~40 ms (cache-hit p50 44 ms in the first benchmark) | Benchmark, then `curl` timing on one connection | On: ~1.1 ms per request |
| Listen backlog | 5: concurrent connects were dropped and retried after 1 s; a concurrency test lost requests in 7 of 10 runs | A concurrency test | 1024 |
| Worker pool | CPU count − 1: idle keep-alive connections hold a worker for up to 5 s, so 32 clients starved new ones (cache-hit p99 107 ms, max 10.3 s) | Benchmark tail, confirmed by rerunning with 8 clients (p99 1.75 ms) | 64 workers |

### Docker packaging

- **Shared builder.** Every service image shares one cached builder stage (Debian bookworm's g++ and CMake),
  and only the build inputs are copied in, so changing docs does not rebuild anything.
- **Runtime.** `debian:bookworm-slim` running as a non-root user, with `curl` for the healthchecks; services
  start in dependency order once healthy.
- **Ports.** Only 8080 and 8090 are published.
- **DNS lookups.** Docker's DNS forwards unknown names (a stopped container) to the host's resolver, which
  took **7.1 s** to fail. Compose now limits lookups to one 1 s attempt and clients ask for IPv4 only. The
  first request after an edge dies went from 4.0 s to 1.0 s, and removal by health checks from 9.0 s to
  1.8 s.

### The dashboard

- **Stack.** Plain HTML, CSS and JavaScript with no framework, build step or external resource, served by
  nginx on 8090. nginx also proxies `/router`, `/edge1..3` and `/origin-admin`, so the page needs no CORS.
- **Raw URIs.** nginx forwards the raw request URI. The usual rewrite would forward a decoded path and break
  keys containing `/` or `?` again.
- **Polling.** The page polls once per second and shows health as colour **and** text.
- **Controls.** It drives the simulated outage, and its load generator is labelled "browser-measured, not a
  benchmark".

### Code rules

- **Style rules.**
  - Single exit point per function (an explicit result variable).
  - No helper functions with only one caller.
  - RAII and smart pointers only.
  - A timeout on every external call.
  - Allman braces.
- **Enforcement.** `scripts/checkStyle.py` and clang-format 18 (`scripts/format.sh`) check the rules in CI.

---

## 7. How we know it works

- **Test-first changes.** Every bug fix and feature started with a test that reproduced it, shown failing.
  The fix followed, then all tests ran and the change was committed. For a data race the failing evidence
  came from ThreadSanitizer, for a performance bug from a measurement.
- **In-process tests.** 140 tests in 5 suites. The edge and router tests start the real servers on ephemeral
  ports against scriptable fake servers, or against the real write-api and storage code in the same process.
- **Sanitizers.** Every suite also runs under AddressSanitizer + UndefinedBehaviorSanitizer and under
  ThreadSanitizer (`-DOUTPOST_SANITIZE=...`), with 0 reports. They found:
  - a null pointer passed to `fwrite`;
  - a data race on request counters;
  - a reference cycle in a test;
  - a temp-file reuse bug.
- **Integration test.** Against the real compose cluster, `scripts/integrationTest.sh` checks:
  - routing correctness and cold-miss caching on the right edge;
  - odd keys stored exactly;
  - 429s under a burst;
  - that only the router port is reachable.
- **Chaos test** (`scripts/chaosTest.sh`). Continuous reads of 200 warm keys and a stream of unique writes
  while storage, then write-api, are killed and restarted. Then an edge is killed **200 ms into replaying its
  queue**.
  - It passes only if every acknowledged write (200/202) is in storage with its exact value, and no warm read
    failed during the outages.
  - Final run: **3714 / 3714** writes persisted; **0** failed reads out of 2476 during outages (1772 served
    STALE); **59** replayed writes recognised as duplicates.
  - A lesson learned: the first version killed the edge with `kill -9 1` from inside its container. The
    kernel ignores signals sent to a container's PID 1 from inside, so the "kill" silently did nothing. The
    script now kills from the host and verifies the container really exited (exit code 137).
- **Benchmark** (`scripts/runBenchmark.sh`). k6 with 32 clients, 90 % GET / 10 % PUT, through the router,
  results saved with the hardware. See `docs/benchmarks.md`.
- **CI.** Build, unit tests, both sanitizer builds, style checks and the compose integration test on every
  push.
- **Fresh clone.** The final check rebuilt and reran everything from an empty directory
  (`docs/finalCheck.md`).

---

## 8. Bugs the measurements found

| Bug | How it showed up | Fix |
|---|---|---|
| A torn WAL tail made every later write unreadable | Unit test: 3 acknowledged writes lost | Cut the invalid tail on open |
| One corrupt length lost the rest of the log | Unit test | CRC over length + payload, resync on the magic number |
| Draining the queue wiped writes queued meanwhile | Test with a slow fake origin | `discardThrough` |
| Replay could overwrite newer data | In-process test with real storage | Versioned last-writer-wins |
| Queued writes stranded until the next outage | In-process test | Drain whenever CLOSED, including at start-up |
| write-api healthy while storage was down | Live run: breaker never opened | Health reflects storage |
| Breaker admitted two probes / could wedge / could deadlock | Unit tests | §3.4 |
| Hash ring split 10 / 56 / 34 % | Distribution test | `fmix64` finalizer |
| Keys with `?` truncated | Live run: `a?b` stored as `a` | Percent-encoding on every hop |
| Data race on counters | ThreadSanitizer | Atomics |
| Null pointer to `fwrite` | UBSan | Skip the write when empty |
| Backlog 5 dropped connections | Concurrency test (7/10 failing) | Backlog 1024 |
| Nagle: ~44 ms per keep-alive request | Benchmark | `TCP_NODELAY` |
| Worker pool starvation | Benchmark tail | 64 workers |
| Services died on SIGTERM; PID 1 ignored it | Shutdown test (exit 143) | `ShutdownSignal` |
| Slow DNS on a dead container stalled failover | Timing measurement (7.1 s lookup) | Bounded lookups |

---

## 9. Trade-offs, limitations and next steps

**Deliberate trade-offs.**
- Availability over freshness while degraded.
- Durability over write latency (fsync per write).
- Wall-clock versions.
- A trusted network: no authentication; `X-Forwarded-For` is believed.

**Limitations.**
- Writes serialize behind one fsync each.
- The storage WAL is never compacted.
- No deletes.
- The router's edge list is static.
- The router and storage are single instances.
- Everything has run on one machine.

**Next steps, in order of value.**
1. **Group commit.** One fsync per batch of writes; the biggest measured bottleneck.
2. **Standard HTTP caching.** `Cache-Control`, `ETag`/`304` revalidation, `stale-if-error`, and a purge API
   that clears a key on every edge.
3. **Multi-region deployment** with Terraform, with cross-region latency measured.
4. **An origin shield.** A middle cache tier, so misses from many edges collapse into one origin request.
5. **Further out.** Gossip-based membership, several routers, replicated storage (consensus), a hybrid
   logical clock, versioned tombstones for deletes, and Prometheus metrics.

---

## 10. Glossary

- **AP / CAP:** during a network partition, a system must choose between consistency and availability;
  Outpost chooses availability.
- **BASE:** Basically Available, Soft state, Eventually consistent; the opposite end from ACID transactions.
- **Cache-aside:** the application checks the cache, and on a miss reads the source of truth and fills the
  cache.
- **Circuit breaker:** stops calls to a failing dependency, then probes it before resuming.
- **Consistent hashing:** maps keys and nodes onto a circle so membership changes move few keys.
- **Virtual node:** one of many points a physical node owns on the ring.
- **Write-ahead log (WAL):** an append-only file to which changes are written before they are acknowledged.
- **fsync:** asks the OS to put written data on stable storage before returning.
- **Torn write:** a record only partly written when the process or machine stopped.
- **Group commit:** many writes share one fsync.
- **Idempotent:** applying the same operation twice has the same effect as once.
- **Last-writer-wins:** the write with the highest version is kept.
- **Stale-if-error:** serving cached data past its freshness lifetime when the origin fails.
- **Thundering herd:** many clients hitting a recovering service at once.
- **Token bucket:** a rate limiter that allows bursts up to a capacity and a long-term average rate.
