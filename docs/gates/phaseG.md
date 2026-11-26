# Phase G record: test inventory cited by README

Run: 2026-11-20T21:12:17Z on `9dc64a9`.

```
common_tests: [  PASSED  ] 30 tests.
storage_tests: [  PASSED  ] 14 tests.
write_api_tests: [  PASSED  ] 14 tests.
edge_tests: [  PASSED  ] 75 tests.
router_tests: [  PASSED  ] 7 tests.
total: 140 tests
$ ctest --test-dir build
100% tests passed, 0 tests failed out of 5
```

## Edge failover timing in compose (measured while writing docs/testing.md)

Measured by killing `edge-2` (`docker compose kill edge-2`) and timing from the host, before and after
`437a3a2` (one 1 s DNS attempt + IPv4-only clients):

```
before: getent hosts edge-2 (stopped) inside the router container: 7111 ms, exit 2
before: first request to an edge-2 key after kill: HTTP 404 after 4004 ms   (served by the new owner)
before: router shows 2 of 3 edges (health checks only) after 8973 ms
after:  getent with timeout:1 attempts:1: 2004 ms (A + AAAA); clients now ask for IPv4 only
after:  first request to an edge-2 key after kill: HTTP 404 after 1017 ms
after:  health checks alone remove edge-2 after 1826 ms
docker compose stop edge-1: 0.33 s
```

## Gate G checks

1. Every measured number in README.md appears in a file under `docs/gates/` or `benchmarks/results/`:

```
140 tests                    docs/gates/phaseG.md
10.4                         docs/gates/phaseC.md
55.6                         docs/gates/phaseC.md
34.0                         docs/gates/phaseC.md
32.0%                        docs/gates/phaseC.md
36.2%                        docs/gates/phaseC.md
31.9%                        docs/gates/phaseC.md
29.3%                        docs/gates/phaseC.md
23.0%                        docs/gates/phaseC.md
16.4%                        docs/gates/phaseC.md
2454 ms                      docs/gates/phaseE.md
3145 ms                      docs/gates/phaseF.md
702 ms                       docs/gates/phaseE.md
3388 / 3388                  docs/gates/phaseE.md
outages: 2112                docs/gates/phaseE.md
STALE: 1626                  docs/gates/phaseE.md
drain start=282              docs/gates/phaseE.md
'duplicates': 41             docs/gates/phaseE.md
2286 requests/s              docs/gates/phaseE.md
| 1.07 | 1.44 | 1.97 |       docs/gates/phaseE.md
115.58 | 126.30 | 220.44     docs/gates/phaseE.md
6.04 | 10.40 | 13.08         benchmarks/results/2026-11-20T205318Z-benchmark/summary.md
7360 requests/s              benchmarks/results/2026-11-20T205318Z-benchmark/summary.md
3777                         docs/gates/phaseD.md
1017 ms                      docs/gates/phaseG.md
1826 ms                      docs/gates/phaseG.md
29.9.3                       docs/gates/phaseF.md
5600H                        docs/gates/phaseB.md
```

2. Every command in README's "How to test it yourself" was run, with its output recorded:

| Command | Recorded in |
|---|---|
| `ctest --test-dir build` | gates C–F, this file |
| ASan + UBSan, TSan `ctest` | `docs/gates/phaseE.md` §1–2 |
| `checkStyle.py`, `format.sh --check` | `docs/gates/phaseD.md` §2, `phaseE.md` §7 |
| `scripts/integrationTest.sh` | `docs/gates/phaseE.md` §4 |
| `scripts/chaosTest.sh` | `docs/gates/phaseE.md` §5 |
| `scripts/runBenchmark.sh 60s 32` | `benchmarks/results/2026-11-20T205135Z-benchmark/` |
| `scripts/dashboardAcceptance.sh` | `docs/gates/phaseF.md` §2 |
| `scripts/shutdownTest.sh` | `docs/gates/phaseD.md` §3 |

All of them run again from a fresh clone in `docs/finalCheck.md`.
