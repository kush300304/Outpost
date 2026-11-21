# Testing checklist

Everything here can be run by hand from a fresh clone. The automated parts are also run by CI and recorded in
`docs/gates/`; the manual parts (mostly the browser checks) are recorded in the run log at the end.

Prerequisites:
- Docker with Compose v2;
- CMake ≥ 3.16 and a C++17 compiler;
- `curl` and `python3` (the scripts use them).

## 1. Automated suites

| # | Check | Command | Expected |
|---|---|---|---|
| 1.1 | Build + unit/in-process tests | `cmake -S . -B build && cmake --build build -j && ctest --test-dir build` | `100% tests passed, 0 tests failed out of 5` |
| 1.2 | ASan + UBSan | `cmake -S . -B build-asan -DOUTPOST_SANITIZE=address,undefined && cmake --build build-asan -j && ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan` | 5/5 suites pass; no `AddressSanitizer`, `LeakSanitizer` or `runtime error` lines |
| 1.3 | TSan | `cmake -S . -B build-tsan -DOUTPOST_SANITIZE=thread && cmake --build build-tsan -j && TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan` | 5/5 suites pass; no `WARNING: ThreadSanitizer` |
| 1.4 | Style | `python3 scripts/checkStyle.py && scripts/format.sh --check` | `0 violations`, `all formatted` |
| 1.5 | Graceful shutdown | `scripts/shutdownTest.sh` | `PASS` for storage, write-api, edge and router |
| 1.6 | Integration (compose) | `scripts/integrationTest.sh` | `integrationTest: 0 failed check(s)` |
| 1.7 | Dashboard acceptance (compose) | `scripts/dashboardAcceptance.sh` | `dashboardAcceptance: 0 failed check(s)` |
| 1.8 | Chaos (compose) | `scripts/chaosTest.sh` | `RESULT: PASS`; `acknowledged writes persisted … N / N`; `failed: 0` during outages |
| 1.9 | Benchmark (compose) | `scripts/runBenchmark.sh 60s 32` | `benchmarks/results/<date>-benchmark/summary.md` with cache-hit p50 well under 10 ms |

On Linux CI runners TSan needs `sudo sysctl vm.mmap_rnd_bits=28` first.

## 2. The spec's demo sequence with curl

Start the cluster with `docker compose up -d --build --wait`. Use a fresh key, or the cache will answer.

- [ ] `curl -i localhost:8080/key/foo` gives `404`, `X-Cache: MISS` (cold: the edge asked the origin).
- [ ] `curl -i -X PUT localhost:8080/key/foo -d bar` gives `200`, `X-Write-Mode: DIRECT`, `X-Node: edge-N`.
- [ ] `curl -i localhost:8080/key/foo` gives `200`, `X-Cache: HIT`, the same `X-Node`.
- [ ] `curl -s localhost:8080/debug/route/foo` names that same edge.
- [ ] `docker compose stop storage`, wait about 3 s, then
      `docker compose exec edge-1 curl -s localhost:8081/debug/stats | grep -A1 '"state"'` shows `OPEN`.
- [ ] `curl -i localhost:8080/key/foo` gives `200`, `X-Cache: STALE`.
- [ ] `curl -i -X PUT localhost:8080/key/foo2 -d queued` gives `202`, `X-Write-Mode: QUEUED`.
- [ ] `docker compose start storage`. Within about 6 s the edge log
      (`docker compose logs edge-N | tail`) shows `HALF_OPEN -> CLOSED` and
      `Reconciling 1 queued writes` / `Reconciliation finished: 1 delivered`.
- [ ] `curl localhost:8080/key/foo2` returns `queued`.

## 3. Dashboard in a browser (http://localhost:8090)

Use a fresh tab (or reload once) after `docker compose up --build`; nginx tells the browser to revalidate the
page and its scripts, so an older cached version is never mixed with a newer one.

Guided demo, from the UI alone:
- [ ] The page loads; the header pill reads `Live · <time> · 3/3 edges answering` and ticks every second.
- [ ] *How a request flows* shows Client → Router → edge-1..3 → write-api → storage, every node **healthy**,
      with the state written as text, not only shown as colour. storage shows its key count.
- [ ] *Run all steps*: the nine steps run in order and each turns into a green ✓ with an explanation:
      1 WRITTEN (path lights up to storage), 2 PURGED, 3 MISS, 4 HIT, 5 304 Not Modified, 6 breakers OPEN
      (about 3 s), 7 STALE, 8 QUEUED, 9 REVALIDATED with the queued value (about 5 s). While it runs, the flow
      diagram shows each request's path and result, the edge cards show the breaker opening and the queue
      rising and draining, and the event log shows the breaker and reconcile events.
- [ ] *Start over with a new key* clears the steps and shows a new demo key; running a single step (for
      example step 4 right after a reset) fails with a red ! and says what it got instead.

Tools:
- [ ] *Send a request*: `GET` of a new key gives MISS · 404; `PUT` gives WRITTEN and names the edge; `GET` gives
      HIT and lists `ETag`, `Age`, `Cache-Control`, `Last-Modified`; ticking *If-None-Match* gives
      304 Not Modified; ticking *no-cache* gives REVALIDATED; *Purge* gives PURGED, then `GET` gives MISS.
- [ ] *Simulated origin outage* switch: on → write-api shows **outage (simulated)** and the edges turn
      **degraded**; off → they return to **healthy**.
- [ ] Mini load generator with 300 / 6 / 50: the coloured bar and the counts add up to 300 and p50/p95/p99
      are shown. With 2000 requests the 429 count becomes non-zero (per-client rate limit).
- [ ] Router card: *Requests routed to each edge* shows three bars of roughly a third each.
- [ ] Origin card: after the load generator or the demo, *Writes per fsync* shows a value for the last 10 s.
- [ ] Event log filters: *Breaker*, *Reconcile*, *Rate limit*, *Purge* show only that type; *All* shows all.

Presentation and accessibility:
- [ ] *Theme* button cycles auto → light → dark and is remembered on reload; in auto it follows the OS. All
      text stays readable and every state is still labelled in words.
- [ ] Phone width: in the DevTools device toolbar at 375 px wide there is no horizontal page scroll, the flow
      diagram turns vertical, and the panels form one column.
- [ ] Keyboard only: Tab reaches every button, input and the GET/PUT/Purge choice in order with a visible focus
      ring; arrow keys switch GET/PUT/Purge; Enter submits the forms; Space or Enter toggles the outage switch
      and the filter chips.
- [ ] Hover (or long-press) a dotted label in an edge card: a tooltip explains it. *What the words mean* opens
      the glossary.
- [ ] DevTools console: no JavaScript errors or warnings. Network-error lines for requests that are meant to
      fail (the `404` cold miss, `503` while degraded) are expected and are not page errors.

Failure views:
- [ ] `docker compose kill edge-2`: within about 2 s edge-2 shows **unreachable** and the router shows
      **2/3 edges**. Keys owned by edge-2 are still served through the other edges. `docker compose start edge-2`
      brings both back to **healthy**.
- [ ] `docker compose stop write-api`: write-api shows **unreachable** and the outage switch is disabled;
      `docker compose start write-api` restores it.

## 4. Durability and recovery by hand

- [ ] `scripts/seedData.sh 300` reports `HTTP 200: 300`.
- [ ] `docker compose kill -s KILL storage && docker compose start storage`. Then
      `docker compose exec storage curl -s localhost:9091/debug/stats` shows `"totalKeys"` including the 300
      seeded keys, and `walRecovery` with `validRecords` > 0.
- [ ] `docker compose logs storage | grep recovered` shows the recovery report (records, keys, skipped and
      truncated bytes).

## 5. Rate limiting by hand

- [ ] `seq 400 | xargs -P 40 -I{} curl -s -o /dev/null -w '%{http_code}\n' -H 'X-Forwarded-For: 198.51.100.1'
      localhost:8080/key/x | sort | uniq -c` shows some `429`.
- [ ] One of those requests made with `curl -i` shows `Retry-After: 1`.
- [ ] Right after the burst, a request with a different `X-Forwarded-For` is not `429`.

## 6. Graceful stop in containers

- [ ] `time docker compose stop edge-1` takes well under Docker's 10 s kill timeout (the services handle
      SIGTERM).
- [ ] `docker compose start edge-1`.

## Run log

| Date | Commit | Who | Sections | Result |
|---|---|---|---|---|
| | | | | |
