#!/usr/bin/env bash
# @file seedData.sh
# Writes COUNT keys (seed-0 … seed-<COUNT-1>) through the router so the cluster has data to read.
#
# Usage: scripts/seedData.sh [COUNT] [BASE_URL]      defaults: 1000, http://localhost:8080
# Requests are spread over 50 client identities (X-Forwarded-For) so the per-client rate limit is
# not the bottleneck, and a 429 is retried after its Retry-After. Prints a count per final status.

set -euo pipefail

COUNT="${1:-1000}"
BASE_URL="${2:-http://localhost:8080}"
declare -A statuses=()

for ((i = 0; i < COUNT; i++)); do
    status=429
    attempt=0
    while [[ "$status" == "429" ]] && ((attempt < 5)); do
        status=$(curl -s -o /dev/null -w '%{http_code}' --max-time 5 -X PUT \
            -H "X-Forwarded-For: 10.200.0.$((i % 50))" \
            --data-binary "value-$i" "$BASE_URL/key/seed-$i" || echo 000)
        if [[ "$status" == "429" ]]; then
            sleep 1
        fi
        attempt=$((attempt + 1))
    done
    statuses[$status]=$((${statuses[$status]:-0} + 1))
done

for status in "${!statuses[@]}"; do
    echo "HTTP $status: ${statuses[$status]}"
done | sort
