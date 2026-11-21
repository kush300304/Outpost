#!/usr/bin/env bash
# @file runBenchmark.sh
# Runs the k6 workload (benchmarks/k6/readHeavy.js) against the compose cluster and saves the real
# results with the hardware they were measured on to benchmarks/results/<date>-benchmark/.
#
# Usage: scripts/runBenchmark.sh [DURATION] [VUS]        defaults: 60s, 32
# WORKLOAD=readHeavy (default, 90 % GET / 10 % PUT) or WORKLOAD=conditional (If-None-Match / 304,
# no-cache revalidation, 4 KiB values; benchmarks/k6/conditional.js) picks the k6 script; for the
# conditional workload WRITE_SHARE (default 0.10) is the share of requests that are PUTs.
# The per-client rate limit is raised for the run (RATE_LIMIT_PER_SEC/BURST) so the limiter is not
# what is being measured, then the defaults are restored. WAL_FSYNC=0 runs a comparison without
# per-write fsync (not a supported production setting).

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
DURATION="${1:-60s}"
VUS="${2:-32}"
WORKLOAD="${WORKLOAD:-readHeavy}"
case "$WORKLOAD" in
    readHeavy)
        SUFFIX=benchmark
        DESCRIPTION="90% GET / 10% PUT over 1000 keys"
        ;;
    conditional)
        SUFFIX=conditional
        WRITE_SHARE="${WRITE_SHARE:-0.10}"
        DESCRIPTION="PUT share $WRITE_SHARE / 5% GET no-cache / the rest GET with If-None-Match once the client has an ETag, over 1000 keys, 4096-byte values"
        ;;
    *)
        echo "unknown WORKLOAD=$WORKLOAD (readHeavy or conditional)" >&2
        exit 2
        ;;
esac
OUT="benchmarks/results/$(date -u +%Y-%m-%dT%H%M%SZ)-$SUFFIX"
mkdir -p "$OUT"

{
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "commit: $(git rev-parse --short HEAD)"
    echo "cpu: $(lscpu | sed -n 's/^Model name: *//p') ($(nproc) threads visible)"
    echo "memory: $(free -h | awk '/Mem:/{print $2}')"
    echo "kernel: $(uname -r)"
    echo "docker: $(docker version --format '{{.Server.Version}}'), $(docker info --format '{{.NCPU}} CPUs, {{.MemTotal}} bytes for containers, {{.OperatingSystem}}')"
    echo "workload: $WORKLOAD.js, $VUS virtual users for $DURATION, $DESCRIPTION, through the router"
    echo "rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)"
    echo "WAL fsync on every write: WAL_FSYNC=${WAL_FSYNC:-1} (1 = durable across power loss)"
} >"$OUT/environment.txt"
cat "$OUT/environment.txt"

WAL_FSYNC="${WAL_FSYNC:-1}" RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 \
    docker compose up -d --wait >/dev/null 2>&1
# Storage counters are cumulative since storage started, so the run's WAL appends and fsyncs are the
# difference between a snapshot taken before k6 and one taken after it.
docker compose exec -T storage curl -s http://localhost:9091/debug/stats >"$OUT/storageStatsBefore.json" || true
docker compose --profile bench run --rm --user "$(id -u):$(id -g)" \
    -e DURATION="$DURATION" -e VUS="$VUS" -e OUT_DIR="/$OUT" -e WRITE_SHARE="${WRITE_SHARE:-0.10}" \
    k6 run --quiet "/benchmarks/k6/$WORKLOAD.js" >"$OUT/k6.txt" 2>&1 || true
docker compose exec -T storage curl -s http://localhost:9091/debug/stats >"$OUT/storageStatsAfter.json" || true
docker compose up -d --wait >/dev/null 2>&1

python3 - "$OUT" "$WORKLOAD" <<'PY' | tee "$OUT/summary.md"
import json, sys
out, workload = sys.argv[1], sys.argv[2]
m = json.load(open(f"{out}/k6-summary.json"))["metrics"]
def count(name):
    return int(m.get(name, {}).get("values", {}).get("count", 0))
def row(name, label, samples):
    v = m.get(name, {}).get("values")
    cells = f"{v['med']:.2f} | {v['p(95)']:.2f} | {v['p(99)']:.2f} | {v['max']:.2f}" if v else "– | – | – | –"
    return f"| {label} | {samples} | {cells} |"
iterations = m["iterations"]["values"]
gets = count("count_get_hit") + count("count_get_miss") + count("count_get_stale")
print("# Benchmark summary\n")
print("```")
print(open(f"{out}/environment.txt").read().rstrip())
print("```\n")
print(f"Measured phase: {int(iterations['count'])} requests, {iterations['rate']:.0f} requests/s "
      f"(k6 iterations; the 1000 setup PUTs are excluded).\n")
print("| Latency (ms) | samples | p50 | p95 | p99 | max |")
print("|---|---|---|---|---|---|")
if workload == "conditional":
    def avg(name):
        v = m.get(name, {}).get("values")
        return f"{v['avg']:.0f}" if v else "–"
    print(row("latency_get_304", "GET, If-None-Match matched: 304, no body", count("count_get_304")))
    print(row("latency_get_changed", "GET, If-None-Match outdated: 200 + new value", count("count_get_changed")))
    print(row("latency_get_hit", "GET, no ETag yet: 200 HIT", count("count_get_hit")))
    print(row("latency_get_revalidated", "GET no-cache: REVALIDATED (origin 304)", count("count_get_revalidated")))
    print(row("latency_get_miss", "GET, X-Cache MISS", count("count_get_miss")))
    print(row("latency_put", "PUT", count("count_put")))
    conditional = count("count_get_conditional")
    nocache = count("count_get_nocache")
    print(f"\nConditional GETs answered 304: {count('count_get_304')} of {conditional}"
          f" ({count('count_get_304') / conditional:.1%})" if conditional else "")
    print(f"no-cache GETs revalidated with a 304 from the origin: {count('count_get_revalidated')} of {nocache}"
          f" ({count('count_get_revalidated') / nocache:.1%})" if nocache else "")
    print(f"Average body bytes: 304 {avg('body_bytes_get_304')}, 200 {avg('body_bytes_get_200')}")
    print(f"Status codes: 2xx {count('status_2xx')}, 304 {count('status_304')}, 404 {count('status_404')}, "
          f"429 {count('status_429')}, 5xx {count('status_5xx')}, other {count('status_other')}")
else:
    print(row("latency_get_hit", "GET, X-Cache HIT", count("count_get_hit")))
    print(row("latency_get_miss", "GET, X-Cache MISS", count("count_get_miss")))
    print(row("latency_get_stale", "GET, X-Cache STALE", count("count_get_stale")))
    print(row("latency_put", "PUT", count("count_put")))
    print(f"\nCache hit ratio of GETs: {count('count_get_hit') / gets:.1%}" if gets else "")
    print(f"Status codes: 2xx {count('status_2xx')}, 404 {count('status_404')}, 429 {count('status_429')}, "
          f"5xx {count('status_5xx')}, other {count('status_other')}")
print(f"k6 http_req_failed rate (includes setup): {m['http_req_failed']['values']['rate']:.4%}")
try:
    before = json.load(open(f"{out}/storageStatsBefore.json")).get("wal", {})
    after = json.load(open(f"{out}/storageStatsAfter.json")).get("wal", {})
    appends = after["appends"] - before["appends"]
    fsyncs = after["fsyncs"] - before["fsyncs"]
    print(f"Storage WAL during this run (setup PUTs included): {appends} appends, {fsyncs} fsyncs "
          f"({appends / max(fsyncs, 1):.2f} appends per fsync)")
except (OSError, ValueError, KeyError):
    pass
PY
echo "benchmark results: $OUT"
