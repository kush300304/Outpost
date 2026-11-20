#!/usr/bin/env bash
# @file runBenchmark.sh
# Runs the k6 workload (benchmarks/k6/readHeavy.js) against the compose cluster and saves the real
# results with the hardware they were measured on to benchmarks/results/<date>-benchmark/.
#
# Usage: scripts/runBenchmark.sh [DURATION] [VUS]        defaults: 60s, 32
# The per-client rate limit is raised for the run (RATE_LIMIT_PER_SEC/BURST) so the limiter is not
# what is being measured, then the defaults are restored. WAL_FSYNC=0 runs a comparison without
# per-write fsync (not a supported production setting).

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
DURATION="${1:-60s}"
VUS="${2:-32}"
OUT="benchmarks/results/$(date -u +%Y-%m-%dT%H%M%SZ)-benchmark"
mkdir -p "$OUT"

{
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "commit: $(git rev-parse --short HEAD)"
    echo "cpu: $(lscpu | sed -n 's/^Model name: *//p') ($(nproc) threads visible)"
    echo "memory: $(free -h | awk '/Mem:/{print $2}')"
    echo "kernel: $(uname -r)"
    echo "docker: $(docker version --format '{{.Server.Version}}'), $(docker info --format '{{.NCPU}} CPUs, {{.MemTotal}} bytes for containers, {{.OperatingSystem}}')"
    echo "workload: $VUS virtual users for $DURATION, 90% GET / 10% PUT over 1000 keys, through the router"
    echo "rate limit during the run: RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 (raised)"
    echo "WAL fsync on every write: WAL_FSYNC=${WAL_FSYNC:-1} (1 = durable across power loss)"
} >"$OUT/environment.txt"
cat "$OUT/environment.txt"

WAL_FSYNC="${WAL_FSYNC:-1}" RATE_LIMIT_PER_SEC=1000000 RATE_LIMIT_BURST=1000000 \
    docker compose up -d --wait >/dev/null 2>&1
docker compose --profile bench run --rm --user "$(id -u):$(id -g)" \
    -e DURATION="$DURATION" -e VUS="$VUS" -e OUT_DIR="/$OUT" \
    k6 run --quiet /benchmarks/k6/readHeavy.js >"$OUT/k6.txt" 2>&1 || true
docker compose up -d --wait >/dev/null 2>&1

python3 - "$OUT" <<'PY' | tee "$OUT/summary.md"
import json, sys
out = sys.argv[1]
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
print(row("latency_get_hit", "GET, X-Cache HIT", count("count_get_hit")))
print(row("latency_get_miss", "GET, X-Cache MISS", count("count_get_miss")))
print(row("latency_get_stale", "GET, X-Cache STALE", count("count_get_stale")))
print(row("latency_put", "PUT", count("count_put")))
print(f"\nCache hit ratio of GETs: {count('count_get_hit') / gets:.1%}" if gets else "")
print(f"Status codes: 2xx {count('status_2xx')}, 404 {count('status_404')}, 429 {count('status_429')}, "
      f"5xx {count('status_5xx')}, other {count('status_other')}")
print(f"k6 http_req_failed rate (includes setup): {m['http_req_failed']['values']['rate']:.4%}")
PY
echo "benchmark results: $OUT"
