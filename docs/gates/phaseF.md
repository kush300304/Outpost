# Phase F exit gate: dashboard

Run: 2026-11-20T21:11:08Z on `7d0257e`. Host: AMD Ryzen 5 5600H with Radeon Graphics, WSL2 + Docker Desktop 29.9.3.

## 1. Dashboard and proxy paths

```
$ docker compose up -d --build --wait
 Container outpost-router-1 Healthy 
$ curl -fsS -o /dev/null -w '%{http_code}' localhost:8090/   → 200
$ curl -fsS -o /dev/null -w '%{http_code}' localhost:8090/router/health   → 200
$ curl -fsS -o /dev/null -w '%{http_code}' localhost:8090/edge1/debug/stats   → 200
$ curl -fsS -o /dev/null -w '%{http_code}' localhost:8090/edge2/debug/stats   → 200
$ curl -fsS -o /dev/null -w '%{http_code}' localhost:8090/edge3/debug/stats   → 200
$ curl -fsS -o /dev/null -w '%{http_code}' localhost:8090/origin-admin/outage   → 200
$ docker compose ps --format "{{json .Publishers}}" | grep -oE "PublishedPort.:[1-9][0-9]*" | sort -u
"PublishedPort":8080
"PublishedPort":8090
```

## 2. Acceptance sequence through the dashboard proxy

```
$ scripts/dashboardAcceptance.sh
PASS  dashboard page is served
PASS  cold GET is a MISS (404, MISS)
PASS  PUT is written directly (DIRECT via edge-3)
PASS  warm GET is a HIT (HIT)
PASS  simulated outage turned on ({"simulatedOutage":true,"storageHealthy":true})
PASS  all breakers OPEN after 3145 ms
PASS  cached read during the outage is STALE (200, STALE)
PASS  write during the outage is queued (202 QUEUED)
PASS  edge-1 shows the queued write (pending=1)
PASS  breakers CLOSED and queue drained after 4768 ms
PASS  queued write reached storage (during-outage)
PASS  event log has breaker and reconcile events (reconcile: finished: 1 delivered, 0 rejected, 0 still queued | breaker: CLOSED -> OPEN | breaker: OPEN -> HALF_OPEN | breaker: HALF_OPEN -> CLOSED | reconcile: started: 1 queued writes | reconcile: finished: 1 delivered, 0 rejected, 0 still queued)
dashboardAcceptance: 0 failed check(s)
exit status: 0
```

## 3. Stats contract, no external resources, page consistency

```
$ ./build/edge/edge_tests --gtest_filter='*StatsMatchTheDocumentedSchema*'
[  PASSED  ] 1 test.
$ grep -nE "https?://" dashboard/index.html dashboard/app.js dashboard/styles.css
(no matches)
$ node --check dashboard/app.js   # the owner's existing Windows Node.js v20.11
syntax OK
$ every id that app.js looks up exists in index.html
19 ids used, missing: none; 11 classes used, missing: none
```

## 4. Browser check

Not run automatically: no browser is available in this environment, and installing a headless browser image is outside the build guide's tool list. The manual check (the acceptance steps from the UI alone, light and dark themes, phone width, keyboard only, DevTools console) is in `docs/testing.md`. Its result is recorded in `docs/finalCheck.md` once the owner has run it.
