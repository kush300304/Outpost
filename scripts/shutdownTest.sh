#!/usr/bin/env bash
# @file shutdownTest.sh
# Starts each service binary, sends SIGTERM, and checks it exits with status 0 within 2 seconds.
#
# Usage: scripts/shutdownTest.sh            (uses build/; BUILD_DIR overrides)
# Exit status is 1 when any service fails the check.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
failures=0

checkShutdown() {
    local name="$1" port="$2"
    shift 2
    env "$@" >"$WORK/$name.log" 2>&1 &
    local pid=$!
    local attempt=0
    until curl -s -o /dev/null --max-time 1 "http://127.0.0.1:$port/health" || ((attempt >= 50)); do
        sleep 0.1
        attempt=$((attempt + 1))
    done
    local start
    start=$(date +%s%N)
    kill -TERM "$pid"
    local waited=0
    while kill -0 "$pid" 2>/dev/null && ((waited < 50)); do
        sleep 0.1
        waited=$((waited + 1))
    done
    local status
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid"
        wait "$pid" 2>/dev/null
        status="still running after 5 s"
    else
        wait "$pid"
        status="exit $?"
    fi
    local elapsed=$((($(date +%s%N) - start) / 1000000))
    if [[ "$status" == "exit 0" && $elapsed -le 2000 ]]; then
        echo "PASS $name: $status after ${elapsed} ms"
    else
        echo "FAIL $name: $status after ${elapsed} ms"
        failures=$((failures + 1))
    fi
}

checkShutdown storage 19191 PORT=19191 WAL_PATH="$WORK/storage.wal" "$BUILD_DIR/storage/storage_server"
checkShutdown write-api 19190 PORT=19190 STORAGE_HOST=127.0.0.1 STORAGE_PORT=19191 \
    "$BUILD_DIR/write-api/write_api_server"
checkShutdown edge 19181 PORT=19181 NODE_ID=edge-t WRITE_API_HOST=127.0.0.1 WRITE_API_PORT=19190 \
    WAL_PATH="$WORK/edge.wal" "$BUILD_DIR/edge/edge_server"
checkShutdown router 19180 PORT=19180 EDGE_NODES="edge-t=127.0.0.1:19181" \
    "$BUILD_DIR/router/router_server"

if ((failures > 0)); then
    echo "shutdownTest: $failures service(s) failed"
    exit 1
fi
echo "shutdownTest: all services shut down cleanly"
