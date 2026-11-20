#!/usr/bin/env bash
# @file format.sh
# Formats (or checks) every project C++ source with the checked-in .clang-format.
#
# Usage:
#   scripts/format.sh           rewrite files in place
#   scripts/format.sh --check   exit 1 and list files whose formatting differs
#
# Uses clang-format 18 (the version on Ubuntu 24.04, as in CI). When clang-format-18 is not
# installed locally it runs inside a small ubuntu:24.04 container built on first use.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="outpost-clang-format:18"
mode="${1:-}"

cd "$ROOT"
mapfile -t files < <(git ls-files 'common/*.h' 'common/*.cpp' 'edge/*.h' 'edge/*.cpp' \
    'router/*.h' 'router/*.cpp' 'storage/*.h' 'storage/*.cpp' 'write-api/*.h' 'write-api/*.cpp')

if command -v clang-format-18 >/dev/null 2>&1; then
    runFormat() { clang-format-18 "$@"; }
else
    if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
        printf 'FROM ubuntu:24.04\nRUN apt-get update && apt-get install -y --no-install-recommends clang-format-18 && rm -rf /var/lib/apt/lists/*\n' |
            docker build -q -t "$IMAGE" - >/dev/null
    fi
    runFormat() { docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:/src" -w /src "$IMAGE" clang-format-18 "$@"; }
fi

if [[ "$mode" == "--check" ]]; then
    report="$(mktemp)"
    trap 'rm -f "$report"' EXIT
    runFormat --dry-run --Werror "${files[@]}" 2>&1 | grep -oE '^[^:]+\.(h|cpp)' | sort -u >"$report" || true
    if [[ -s "$report" ]]; then
        echo "clang-format: these files need formatting (run scripts/format.sh):"
        cat "$report"
        exit 1
    fi
    echo "clang-format: ${#files[@]} files, all formatted"
else
    runFormat -i "${files[@]}"
    echo "clang-format: formatted ${#files[@]} files"
fi
