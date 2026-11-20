#!/usr/bin/env bash
# @file dashboardAcceptance.sh
# Runs the dashboard spec's acceptance sequence through the dashboard's own proxy paths (the same
# requests the page makes): cold miss, warm hit, simulated outage on, breaker OPEN and STALE reads,
# a queued write, outage off, breaker CLOSED and the queue drained, events in the edge's log.
#
# Usage: scripts/dashboardAcceptance.sh      (DASHBOARD=http://localhost:8090 by default)
# Exit status is the number of failed checks.

set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
DASHBOARD="${DASHBOARD:-http://localhost:8090}"
failures=0
key="accept-$(date +%s)"

check() {
    if [[ "$2" == "true" ]]; then
        echo "PASS  $1"
    else
        echo "FAIL  $1"
        failures=$((failures + 1))
    fi
}
nowMs() { echo $(($(date +%s%N) / 1000000)); }
headers() { curl -s -D - -o /dev/null "$@" | tr -d '\r'; }
headerValue() { awk -v h="$1" 'tolower($1)==tolower(h":") {print $2}'; }
statusOf() { awk '/^HTTP\//{print $2}'; }
edgeJson() { curl -s "$DASHBOARD/edge${1#edge-}/debug/stats"; }
field() { python3 -c "import sys,json;d=json.load(sys.stdin);print($1)"; }
allStates() {
    local want="$1"
    for e in 1 2 3; do
        [[ "$(edgeJson "edge-$e" | field 'd["circuitBreaker"]["state"]')" == "$want" ]] || return 1
    done
}
waitUntil() {
    local start
    start=$(nowMs)
    until eval "$2"; do
        if (($(nowMs) - start > $1 * 1000)); then
            echo "(timed out after $1 s)"
            return 1
        fi
        sleep 0.2
    done
    echo "$(($(nowMs) - start)) ms"
}

docker compose up -d --wait >/dev/null 2>&1 || { echo "FAIL  docker compose up"; exit 1; }
curl -s -o /dev/null -X POST -H 'Content-Type: application/json' -d '{"enabled":false}' \
    "$DASHBOARD/origin-admin/outage"
check "dashboard page is served" "$([[ "$(curl -s -o /dev/null -w '%{http_code}' "$DASHBOARD/")" == 200 ]] && echo true)"

# 1. Cold miss, then warm hit.
cold=$(headers "$DASHBOARD/router/key/$key")
check "cold GET is a MISS ($(echo "$cold" | statusOf), $(echo "$cold" | headerValue X-Cache))" \
    "$([[ "$(echo "$cold" | headerValue X-Cache)" == MISS ]] && echo true)"
put=$(headers -X PUT --data-binary "before-outage" "$DASHBOARD/router/key/$key")
owner=$(echo "$put" | headerValue X-Node)
check "PUT is written directly ($(echo "$put" | headerValue X-Write-Mode) via $owner)" \
    "$([[ "$(echo "$put" | headerValue X-Write-Mode)" == DIRECT ]] && echo true)"
warm=$(headers "$DASHBOARD/router/key/$key")
check "warm GET is a HIT ($(echo "$warm" | headerValue X-Cache))" \
    "$([[ "$(echo "$warm" | headerValue X-Cache)" == HIT ]] && echo true)"

# 2. Simulated outage on: every breaker opens, cached reads are STALE, writes queue.
on=$(curl -s -X POST -H 'Content-Type: application/json' -d '{"enabled":true}' "$DASHBOARD/origin-admin/outage")
check "simulated outage turned on ($on)" "$(echo "$on" | field 'str(d["simulatedOutage"]).lower()')"
opened=$(waitUntil 20 'allStates OPEN')
check "all breakers OPEN after $opened" "$([[ "$opened" != *timed* ]] && echo true)"
stale=$(headers "$DASHBOARD/router/key/$key")
check "cached read during the outage is STALE ($(echo "$stale" | statusOf), $(echo "$stale" | headerValue X-Cache))" \
    "$([[ "$(echo "$stale" | statusOf)" == 200 && "$(echo "$stale" | headerValue X-Cache)" == STALE ]] && echo true)"
queued=$(headers -X PUT --data-binary "during-outage" "$DASHBOARD/router/key/$key-queued")
queuedOwner=$(echo "$queued" | headerValue X-Node)
check "write during the outage is queued ($(echo "$queued" | statusOf) $(echo "$queued" | headerValue X-Write-Mode))" \
    "$([[ "$(echo "$queued" | statusOf)" == 202 && "$(echo "$queued" | headerValue X-Write-Mode)" == QUEUED ]] && echo true)"
pending=$(edgeJson "$queuedOwner" | field 'd["writeQueue"]["pending"]')
check "$queuedOwner shows the queued write (pending=$pending)" "$([[ "$pending" -ge 1 ]] && echo true)"

# 3. Outage off: breakers close and the queue drains into storage.
curl -s -o /dev/null -X POST -H 'Content-Type: application/json' -d '{"enabled":false}' "$DASHBOARD/origin-admin/outage"
closed=$(waitUntil 30 'allStates CLOSED && [[ "$(edgeJson "$queuedOwner" | field "d[\"writeQueue\"][\"pending\"]")" == 0 ]]')
check "breakers CLOSED and queue drained after $closed" "$([[ "$closed" != *timed* ]] && echo true)"
stored=$(docker compose exec -T storage curl -s "http://localhost:9091/key/$key-queued" | field 'd.get("value","")')
check "queued write reached storage ($stored)" "$([[ "$stored" == during-outage ]] && echo true)"

# 4. The event log shows what happened.
events=$(edgeJson "$queuedOwner" | field '" | ".join(e["type"]+": "+e["message"] for e in d["events"][-6:])')
check "event log has breaker and reconcile events ($events)" \
    "$([[ "$events" == *"breaker: CLOSED -> OPEN"* && "$events" == *"reconcile: finished"* ]] && echo true)"

echo "dashboardAcceptance: $failures failed check(s)"
exit "$failures"
