# 0001. Availability over consistency during an origin outage

**Status:** accepted

## Context
The edges sit in front of one authoritative origin (write-api + storage). When the origin is unreachable, an
edge can either refuse requests (consistent but unavailable) or keep answering (available but possibly stale).
The project's purpose is to keep serving through origin failures.

## Decision
Choose availability (AP in CAP terms, BASE rather than ACID). While the origin is down:
- **reads** of cached keys are served as `X-Cache: STALE`;
- **writes** are accepted into a durable queue at the edge (`202`) and delivered in order when the origin is
  back.

Values converge through versioned last-writer-wins ([0002](0002-versioned-last-writer-wins.md)).

## Alternatives
- **Refuse everything while the origin is down.** Simple and always consistent, but an outage becomes a full
  outage for users.
- **Serve reads only, refuse writes.** This is what a typical CDN does. Still loses every write made during the
  outage.

## Consequences
- Clients can read a value that has since changed elsewhere. Staleness is bounded by the cache TTL (60 s) when
  healthy, and by ttl + `stale-if-error` (1 h) during an outage.
- A `202` means "durable at the edge, delivered later", not "visible everywhere now".
- **Evidence:** the chaos test kept every acknowledged write and had no failed warm read during origin outages
  (`docs/finalCheck.md`).
