# Changelog

All notable changes are listed here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [semantic versioning](https://semver.org/).

## [1.0.1] - 2026-11-21

### Fixed
- **Purge waited 5 s when a client sent no body.** `POST /purge/key/<k>` without a `Content-Length` (what
  `curl -X POST` sends) waited 5 s for a request body, until the HTTP library's read timeout. It now answers at
  once (about 30 ms), as HTTP/1.1 requires for a request with no length.
- **The integration test's rate-limit check depended on machine speed.** It failed on a slow CI runner whose 400
  separate `curl` processes took 2.5 s, long enough for the token bucket to refill. It now sends 600 requests
  from one `curl` over keep-alive connections, in about 0.15 s, and prints how long the burst took.

### Added
- An integration check that a purge without a request body answers in under 1 s (22 checks in total).
- A router test that sends exactly that request over a raw socket.

## [1.0.0] - 2026-11-21

First public release.

### Added
- **Hand-built core** (`common/`, no third-party code):
  - consistent hash ring (150 virtual nodes, FNV-1a with an `fmix64` finalizer);
  - LRU cache with TTL, versions and metadata;
  - per-client token bucket;
  - circuit breaker with a single half-open probe;
  - write-ahead log with CRC-checked records, torn-tail recovery and group commit;
  - helpers for entity tags, URL encoding, request limits and graceful shutdown.
- **Router:** stateless consistent-hash routing, health-driven membership, failover retry, purge fan-out to
  every edge, and pass-through of the HTTP caching headers.
- **Edges:**
  - cache-aside reads, plus standard HTTP caching (`ETag`, `Last-Modified`, `Age`, `Cache-Control` with
    `stale-if-error`), `304` for `If-None-Match`, revalidation with the origin, and `no-cache`;
  - per-client rate limiting (`429` + `Retry-After`);
  - degraded mode: `STALE` reads, and writes queued in a durable WAL that drains in order once the origin
    recovers.
- **write-api:** validation, a health check that reflects storage, `502` for storage failures, conditional-read
  pass-through, and a simulated-outage switch for the dashboard.
- **storage:** a versioned key-value store (last writer wins on `(version, writerId)`, so replays are idempotent)
  made durable by its WAL, with group commit and `304` answers.
- **Dashboard:** a live request-flow diagram, a self-checking nine-step guided demo, per-service panels, request
  tools, a filterable event log, and light and dark themes.
- **Verification:**
  - 163 unit and in-process tests, plus ASan/UBSan and TSan builds;
  - an integration test (21 checks), dashboard acceptance (18 checks) and a headless-browser click test;
  - a chaos test that kills services under load, a graceful-shutdown test, and k6 benchmarks (closed loop,
    fixed rate, conditional requests);
  - CI on every push.
- **Operations:** Docker Compose with health checks, only ports 8080 and 8090 published, log rotation and
  `LOG_LEVEL`.
- **Documentation:** README, architecture, design walkthrough, hands-on tour, benchmarks, the stats API, gate
  records and final checks.
