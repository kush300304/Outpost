# 0006. Circuit breaker thresholds

**Status:** accepted

## Context
Without a breaker, every request that needs a dead origin waits for a timeout and holds a worker thread. Under
load the edges run out of workers and fail even cache hits, and the retries flood the origin when it tries to
recover.

## Decision
- **One breaker per edge, driven by a 1 s heartbeat** to write-api's `/health`, which reflects storage.
  Failed requests count too.
- **Opening:** 3 consecutive failures open it.
- **Probing:** after 5 s it admits exactly **one** half-open probe. Success closes it; failure reopens it for
  another 5 s.
- **Lost probes:** a probe whose result is never recorded is replaced after the same timeout, so the breaker
  can't get stuck half-open.

## Alternatives
- **An error-rate window** instead of consecutive failures. Better for flaky dependencies, but more state;
  listed as a next step.
- **Lower or higher thresholds.** Lower ones react faster but flap on a 1-second blip; higher ones serve errors
  for longer.

## Consequences
- Outages are detected in about 1–6 s (measured 0.7–5.3 s across runs).
- A recovering origin sees one probe, not a stampede.
- A single failed health check, such as a storage restart, doesn't trip it (seen in the logs).
