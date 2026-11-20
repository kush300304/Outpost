#!/usr/bin/env bash
# @file integrationTest.sh
# End-to-end checks against the Docker Compose cluster (started here if it is not running).
#
# Usage: scripts/integrationTest.sh        (ROUTER=http://localhost:8080 by default)
# Prints PASS/FAIL per check; exit status is the number of failed checks (0 = all passed).

set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
ROUTER="${ROUTER:-http://localhost:8080}"
failures=0
run="it-$(date +%s)"

check() {
    local name="$1" condition="$2"
    if [[ "$condition" == "true" ]]; then
        echo "PASS  $name"
    else
        echo "FAIL  $name"
        failures=$((failures + 1))
    fi
}
header() { curl -s -D - -o /dev/null "$@" | tr -d '\r' | awk -v h="$HEADER" 'tolower($1)==tolower(h":") {print $2}'; }
routeOf() { curl -s "$ROUTER/debug/route/$1" | python3 -c 'import sys,json;print(json.load(sys.stdin)["node"])'; }
storageGet() { docker compose exec -T storage curl -s "http://localhost:9091/key/$1"; }

docker compose up -d --wait >/dev/null 2>&1 || { echo "FAIL  docker compose up"; exit 1; }

# 1. Router sees all three edges.
ring=$(curl -s "$ROUTER/health" | python3 -c 'import sys,json;print(json.load(sys.stdin)["edgesInRing"])')
check "router has 3 edges on the ring (got $ring)" "$([[ "$ring" == 3 ]] && echo true)"

# 2. A write lands on the edge the ring names, and storage holds it.
key="$run-write"
HEADER=X-Node node=$(header -X PUT --data-binary hello "$ROUTER/key/$key")
owner=$(routeOf "$key")
check "PUT served by the ring owner ($node == $owner)" "$([[ -n "$node" && "$node" == "$owner" ]] && echo true)"
stored=$(storageGet "$key" | python3 -c 'import sys,json;print(json.load(sys.stdin).get("value",""))')
check "storage holds the written value ($stored)" "$([[ "$stored" == hello ]] && echo true)"
HEADER=X-Cache cache=$(header "$ROUTER/key/$key")
check "read after write is a cache HIT ($cache)" "$([[ "$cache" == HIT ]] && echo true)"

# 3. A cold miss reaches storage and caches on the right edge.
key="$run-cold"
version=$(($(date +%s%N) / 1000))
docker compose exec -T storage curl -s -X PUT -d "{\"value\":\"cold\",\"version\":$version,\"writerId\":\"it\"}" \
    "http://localhost:9091/key/$key" >/dev/null
owner=$(routeOf "$key")
HEADER=X-Cache first=$(header "$ROUTER/key/$key")
HEADER=X-Node firstNode=$(header "$ROUTER/key/$key")
HEADER=X-Cache second=$(header "$ROUTER/key/$key")
check "cold read is a MISS, then HIT ($first, $second)" "$([[ "$first" == MISS && "$second" == HIT ]] && echo true)"
check "cold key is cached on its ring owner ($firstNode == $owner)" "$([[ "$firstNode" == "$owner" ]] && echo true)"

# 4. Keys with reserved characters round-trip unchanged.
encoded="$run%3Fa%20b%2Fc%25%23%2B"
curl -s -o /dev/null -X PUT --data-binary tricky "$ROUTER/key/$encoded"
stored=$(storageGet "$encoded" | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d.get("key",""), d.get("value",""))')
check "key '$run?a b/c%#+' stored exactly ($stored)" "$([[ "$stored" == "$run?a b/c%#+ tricky" ]] && echo true)"

# 5. Per-client rate limiting under a burst.
burst=$(seq 400 | xargs -P 40 -I{} curl -s -D - -o /dev/null -H "X-Forwarded-For: 198.51.100.77" \
    "$ROUTER/key/$run-hot" | tr -d '\r')
codes=$(echo "$burst" | awk '/^HTTP\//{print $2}' | sort | uniq -c | tr '\n' ' ')
limited=$(echo "$burst" | grep -c '^HTTP/1.1 429')
retries=$(echo "$burst" | grep -ci '^retry-after: 1$')
check "burst from one client is rate-limited ($codes)" "$([[ -n "$limited" && "$limited" -gt 0 ]] && echo true)"
other=$(curl -s -o /dev/null -w '%{http_code}' -H "X-Forwarded-For: 198.51.100.78" "$ROUTER/key/$run-hot")
check "another client is still served ($other)" "$([[ "$other" != 429 ]] && echo true)"
check "every 429 carries Retry-After: 1 ($retries of $limited)" "$([[ "$retries" == "$limited" ]] && echo true)"

# 6. Only the router is reachable from the host.
for port in 8081 8082 8083 9090 9091; do
    code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 2 "http://localhost:$port/health")
    check "port $port is not published (curl got $code)" "$([[ "$code" == 000 ]] && echo true)"
done

echo "integrationTest: $failures failed check(s)"
exit "$failures"
