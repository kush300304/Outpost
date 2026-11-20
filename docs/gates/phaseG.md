# Phase G record: test inventory cited by README

Run: 2026-11-20T21:12:17Z on `d3bf7a9`.

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
`b7101ad` (one 1 s DNS attempt + IPv4-only clients):

```
before: getent hosts edge-2 (stopped) inside the router container: 7111 ms, exit 2
before: first request to an edge-2 key after kill: HTTP 404 after 4004 ms   (served by the new owner)
before: router shows 2 of 3 edges (health checks only) after 8973 ms
after:  getent with timeout:1 attempts:1: 2004 ms (A + AAAA); clients now ask for IPv4 only
after:  first request to an edge-2 key after kill: HTTP 404 after 1017 ms
after:  health checks alone remove edge-2 after 1826 ms
docker compose stop edge-1: 0.33 s
```
