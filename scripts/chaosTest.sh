#!/usr/bin/env bash
# @file chaosTest.sh
# Chaos test against the Docker Compose cluster, under continuous read and write load:
#   1. kill -9 storage, hold, restart it
#   2. kill -9 write-api, hold, restart it
#   3. queue writes, then kill -9 the edge with the most queued writes while it drains, restart it
# Pass criteria (the spec's "zero write loss" and "reads never hard-fail"):
#   - every write the cluster acknowledged (200 or 202) is in storage with its exact value
#   - no read of a warm (previously written) key fails during the storage and write-api outages
# Results go to stdout and to benchmarks/results/<date>-chaos/ (raw logs plus summary.txt).
#
# Usage: scripts/chaosTest.sh            (ROUTER=http://localhost:8080 by default)

set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
ROUTER="${ROUTER:-http://localhost:8080}"
RUN="chaos$(date +%s)"
OUT="benchmarks/results/$(date -u +%Y-%m-%dT%H%M%SZ)-chaos"
WARM_KEYS=200
mkdir -p "$OUT"
: >"$OUT/writes.log"
: >"$OUT/reads.log"
: >"$OUT/timeline.log"

nowMs() { echo $(($(date +%s%N) / 1000000)); }
mark() { echo "$(nowMs) $*" | tee -a "$OUT/timeline.log"; }
edgeStat() {
    local edge="$1" expression="$2" port=$((8080 + ${1#edge-}))
    docker compose exec -T "$edge" curl -s "http://localhost:$port/debug/stats" |
        python3 -c "import sys,json;d=json.load(sys.stdin);print($expression)" 2>/dev/null
}
waitFor() {
    local description="$1" timeoutSeconds="$2" check="$3" start
    start=$(nowMs)
    until eval "$check"; do
        if (($(nowMs) - start > timeoutSeconds * 1000)); then
            mark "TIMEOUT waiting for: $description"
            return 1
        fi
        sleep 0.2
    done
    mark "$description after $(($(nowMs) - start)) ms"
}
allEdges() { local want="$1" expr="$2"; for e in edge-1 edge-2 edge-3; do [[ "$(edgeStat "$e" "$expr")" == "$want" ]] || return 1; done; }
allBreakers() { allEdges "$1" 'd["circuitBreaker"]["state"]'; }
allDrained() { allEdges 0 'd["writeQueue"]["pending"]'; }

docker compose up -d --wait >/dev/null 2>&1 || { echo "docker compose up failed"; exit 1; }
mark "start run=$RUN"

# Warm keys: written once (cached on their owner edge), then read continuously.
for ((i = 0; i < WARM_KEYS; i++)); do
    curl -s -o /dev/null -X PUT -H "X-Forwarded-For: 10.66.0.$((i % 50))" --data-binary "warm-$i" \
        "$ROUTER/key/$RUN-warm-$i"
done
mark "seeded $WARM_KEYS warm keys"

writer() {
    local i=0
    while [[ ! -f "$OUT/stop" ]]; do
        local code
        code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 5 -X PUT \
            -H "X-Forwarded-For: 10.77.0.$((i % 50))" --data-binary "value-$i" "$ROUTER/key/$RUN-w-$i")
        echo "$(nowMs) $i $code" >>"$OUT/writes.log"
        i=$((i + 1))
    done
}
reader() {
    local i=0
    while [[ ! -f "$OUT/stop" ]]; do
        local k=$((i % WARM_KEYS)) result
        result=$(curl -s -o /dev/null -D - --max-time 5 -H "X-Forwarded-For: 10.88.0.$((i % 50))" \
            "$ROUTER/key/$RUN-warm-$k" | tr -d '\r' |
            awk '/^HTTP\//{code=$2} tolower($1)=="x-cache:"{cache=$2} END{print code" "cache}')
        echo "$(nowMs) $k $result" >>"$OUT/reads.log"
        i=$((i + 1))
    done
}
writer &
writerPid=$!
reader &
readerPid=$!
sleep 5

# 1. Storage outage.
docker compose kill -s KILL storage >/dev/null 2>&1
mark "OUTAGE-START storage killed"
waitFor "all breakers OPEN (storage down)" 30 'allBreakers OPEN'
sleep 8
docker compose start storage >/dev/null 2>&1
mark "storage restarted"
waitFor "all breakers CLOSED and queues drained" 60 'allBreakers CLOSED && allDrained'
mark "OUTAGE-END storage"
sleep 3

# 2. write-api outage.
docker compose kill -s KILL write-api >/dev/null 2>&1
mark "OUTAGE-START write-api killed"
waitFor "all breakers OPEN (write-api down)" 30 'allBreakers OPEN'
sleep 8
docker compose start write-api >/dev/null 2>&1
mark "write-api restarted"
waitFor "all breakers CLOSED and queues drained" 60 'allBreakers CLOSED && allDrained'
mark "OUTAGE-END write-api"
sleep 3

# 3. Kill an edge in the middle of draining its queue.
docker compose kill -s KILL write-api >/dev/null 2>&1
mark "write-api killed to queue writes"
sleep 12
victim=edge-1
most=-1
for e in edge-1 edge-2 edge-3; do
    pending=$(edgeStat "$e" 'd["writeQueue"]["pending"]')
    mark "$e pending=$pending"
    if ((pending > most)); then
        most=$pending
        victim=$e
    fi
done
port=$((8080 + ${victim#edge-}))
startsBefore=$(edgeStat "$victim" 'sum(1 for e in d["events"] if e["message"].startswith("started"))')
# Detect the drain from inside the victim container (polling its own stats every 10 ms), then kill
# it from the host 200 ms later. Writes sent by then are in storage but still in the edge's WAL,
# so the restarted edge replays them and storage must answer duplicate. (A kill sent from inside
# the container would be ignored: the edge is PID 1 there and the kernel drops SIGKILL to it.)
docker compose exec -T "$victim" sh -c "
    while true; do
        stats=\$(curl -s localhost:$port/debug/stats)
        starts=\$(echo \"\$stats\" | grep -c '\"message\": \"started')
        if [ \"\$starts\" -gt $startsBefore ]; then
            echo \"\$stats\" | sed -n 's/.*\"pending\": \([0-9]*\).*/queued at drain start=\1/p'
            break
        fi
        sleep 0.01
    done" >"$OUT/edgeKill.txt" 2>&1 &
watcherPid=$!
docker compose start write-api >/dev/null 2>&1
mark "write-api restarted; $victim (pending=$most) will be killed 200 ms into its drain"
wait "$watcherPid"
sleep 0.2
docker compose kill -s KILL "$victim" >/dev/null 2>&1
state=$(docker inspect -f '{{.State.Status}} exit={{.State.ExitCode}}' "$(docker compose ps -a -q "$victim")")
mark "EDGE-KILL $victim killed mid-drain ($(head -1 "$OUT/edgeKill.txt"); container now: $state)"
sleep 2
docker compose start "$victim" >/dev/null 2>&1
mark "$victim restarted"
waitFor "all breakers CLOSED and queues drained" 60 'allBreakers CLOSED && allDrained'
docker compose logs --since 30s "$victim" 2>&1 | grep -E "WAL|Reconcil" | sed 's/^/    /' | tee -a "$OUT/timeline.log"
mark "EDGE-KILL-END"
sleep 3

touch "$OUT/stop"
wait "$writerPid" "$readerPid"
rm -f "$OUT/stop"
waitFor "final drain" 60 'allDrained'
mark "load stopped"

# Verification: fetch every acknowledged key from storage inside its container.
awk '$3 == 200 || $3 == 202 {print $2}' "$OUT/writes.log" | sed "s/^/$RUN-w-/" >"$OUT/acknowledged.txt"
docker compose exec -T storage bash -c \
    'while read -r key; do printf "%s " "$key"; curl -s "http://localhost:9091/key/$key"; echo; done' \
    <"$OUT/acknowledged.txt" >"$OUT/storage.txt"
docker compose exec -T storage curl -s http://localhost:9091/debug/stats >"$OUT/storageStats.json"

python3 - "$OUT" "$RUN" <<'PY' | tee "$OUT/summary.txt"
import json, sys
out, run = sys.argv[1], sys.argv[2]
writes = [line.split() for line in open(f"{out}/writes.log")]
acked = {f"{run}-w-{w[1]}": f"value-{w[1]}" for w in writes if w[2] in ("200", "202")}
other = {}
for w in writes:
    if w[2] not in ("200", "202"):
        other[w[2]] = other.get(w[2], 0) + 1
stored = {}
for line in open(f"{out}/storage.txt"):
    key, _, body = line.partition(" ")
    try:
        stored[key] = json.loads(body).get("value")
    except ValueError:
        stored[key] = None
missing = [k for k, v in acked.items() if stored.get(k) != v]

timeline = [line.split(" ", 1) for line in open(f"{out}/timeline.log")]
windows, start = [], None
for ms, text in timeline:
    if text.startswith("OUTAGE-START"):
        start = int(ms)
    elif text.startswith("OUTAGE-END") and start is not None:
        windows.append((start, int(ms)))
        start = None
edgeKill = [int(ms) for ms, text in timeline if text.startswith(("EDGE-KILL ", "EDGE-KILL-END"))]
reads = [line.split() for line in open(f"{out}/reads.log")]
def inside(ms, spans):
    return any(a <= ms <= b for a, b in spans)
outageReads = [r for r in reads if inside(int(r[0]), windows)]
failedOutage = [r for r in outageReads if len(r) < 3 or r[2] != "200"]
staleOutage = sum(1 for r in outageReads if len(r) > 3 and r[3] == "STALE")
killSpan = [(edgeKill[0], edgeKill[1])] if len(edgeKill) == 2 else []
killReads = [r for r in reads if inside(int(r[0]), killSpan)]
failedKill = [r for r in killReads if len(r) < 3 or r[2] != "200"]
stats = json.load(open(f"{out}/storageStats.json"))

print(f"writes sent: {len(writes)}  acknowledged (200/202): {len(acked)}  not acknowledged: {other}")
print(f"acknowledged writes persisted with the exact value: {len(acked) - len(missing)} / {len(acked)}")
print(f"reads of warm keys: {len(reads)} total; during storage/write-api outages: {len(outageReads)}, "
      f"failed: {len(failedOutage)}, served STALE: {staleOutage}")
print(f"reads during the edge-kill window (reported, not asserted): {len(killReads)}, failed: {len(failedKill)}")
print(f"storage puts since its last restart: {stats['puts']} "
      "(duplicates = queued writes replayed after the mid-drain edge kill, applied only once)")
ok = not missing and not failedOutage and len(acked) > 0 and len(windows) == 2
print("RESULT: PASS" if ok else f"RESULT: FAIL (missing={missing[:5]}, failed outage reads={failedOutage[:5]})")
sys.exit(0 if ok else 1)
PY
status=${PIPESTATUS[0]}
echo "chaos results: $OUT"
exit "$status"
