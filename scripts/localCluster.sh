#!/usr/bin/env bash
# @file localCluster.sh
# Runs the Outpost services locally, without Docker, from a CMake build directory.
#
# Usage:
#   scripts/localCluster.sh start              start every service (storage first)
#   scripts/localCluster.sh stop               stop every service (SIGTERM)
#   scripts/localCluster.sh status             show which services are running
#   scripts/localCluster.sh kill <service>     SIGKILL one service (simulated crash)
#   scripts/localCluster.sh start <service>    start one service
#
# Services and ports: storage 9091, write-api 9090, edge-1..3 8081..8083, router 8080.
# Environment: BUILD_DIR (default build), RUN_DIR (default $BUILD_DIR/localCluster).
# Extra settings are passed through, e.g. BREAKER_RESET_TIMEOUT_MS=2000.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
RUN_DIR="${RUN_DIR:-$BUILD_DIR/localCluster}"
ALL_SERVICES=(storage write-api edge-1 edge-2 edge-3)
if [[ -x "$BUILD_DIR/router/router_server" ]]; then
    ALL_SERVICES+=(router)
fi
mkdir -p "$RUN_DIR"

servicePort() {
    case "$1" in
        storage) echo 9091 ;;
        write-api) echo 9090 ;;
        edge-1) echo 8081 ;;
        edge-2) echo 8082 ;;
        edge-3) echo 8083 ;;
        router) echo 8080 ;;
        *) echo "unknown service: $1" >&2; exit 2 ;;
    esac
}

isRunning() {
    local pidFile="$RUN_DIR/$1.pid"
    [[ -f "$pidFile" ]] && kill -0 "$(cat "$pidFile")" 2>/dev/null
}

startService() {
    local service="$1"
    local port
    port="$(servicePort "$service")"
    if isRunning "$service"; then
        echo "$service already running"
    else
        case "$service" in
            storage)
                env PORT="$port" WAL_PATH="$RUN_DIR/storage.wal" \
                    "$BUILD_DIR/storage/storage_server" >>"$RUN_DIR/$service.log" 2>&1 &
                ;;
            write-api)
                env PORT="$port" STORAGE_HOST=127.0.0.1 STORAGE_PORT=9091 \
                    "$BUILD_DIR/write-api/write_api_server" >>"$RUN_DIR/$service.log" 2>&1 &
                ;;
            edge-*)
                env PORT="$port" NODE_ID="$service" WRITE_API_HOST=127.0.0.1 WRITE_API_PORT=9090 \
                    WAL_PATH="$RUN_DIR/$service.wal" \
                    "$BUILD_DIR/edge/edge_server" >>"$RUN_DIR/$service.log" 2>&1 &
                ;;
            router)
                env PORT="$port" \
                    EDGE_NODES="edge-1=127.0.0.1:8081,edge-2=127.0.0.1:8082,edge-3=127.0.0.1:8083" \
                    "$BUILD_DIR/router/router_server" >>"$RUN_DIR/$service.log" 2>&1 &
                ;;
        esac
        echo $! >"$RUN_DIR/$service.pid"
        local attempt=0
        until curl -s -o /dev/null --max-time 1 "http://127.0.0.1:$port/health" || ((attempt >= 50)); do
            sleep 0.1
            attempt=$((attempt + 1))
        done
        echo "$service started (pid $(cat "$RUN_DIR/$service.pid"), port $port)"
    fi
}

stopService() {
    local service="$1"
    local signal="$2"
    if isRunning "$service"; then
        kill "-$signal" "$(cat "$RUN_DIR/$service.pid")"
        local attempt=0
        while isRunning "$service" && ((attempt < 50)); do
            sleep 0.1
            attempt=$((attempt + 1))
        done
        echo "$service stopped (SIG$signal)"
    else
        echo "$service not running"
    fi
    rm -f "$RUN_DIR/$service.pid"
}

command="${1:-}"
target="${2:-}"
case "$command" in
    start)
        if [[ -n "$target" ]]; then
            startService "$target"
        else
            for service in "${ALL_SERVICES[@]}"; do
                startService "$service"
            done
        fi
        ;;
    stop)
        services=("${ALL_SERVICES[@]}")
        if [[ -n "$target" ]]; then
            services=("$target")
        fi
        for ((i = ${#services[@]} - 1; i >= 0; i--)); do
            stopService "${services[$i]}" TERM
        done
        ;;
    kill)
        stopService "$target" KILL
        ;;
    status)
        for service in "${ALL_SERVICES[@]}"; do
            if isRunning "$service"; then
                echo "$service: running (port $(servicePort "$service"))"
            else
                echo "$service: stopped"
            fi
        done
        ;;
    *)
        sed -n '3,14p' "$0"
        exit 2
        ;;
esac
