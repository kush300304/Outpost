# 0002. Versioned last-writer-wins writes

**Status:** accepted

## Context
Queued writes are replayed after an outage, possibly twice (an edge can crash mid-replay), and possibly after a
newer write to the same key has already landed. Replays must be idempotent and must never overwrite newer data.

## Decision
- **Stamping:** every write is stamped at the edge with `(version, writerId)`. The version is microseconds,
  strictly increasing per edge; the writer id is the edge's id.
- **Comparison:** storage keeps the pair of the value it holds and compares the incoming pair:
  - newer → `applied`;
  - equal → `duplicate`, nothing changes;
  - older → `superseded`, nothing changes.
- **Replay:** only applied writes are logged, and replay uses the same rule.
- **ETag:** the same pair is the value's HTTP ETag ([0008](0008-standard-http-caching-and-purge.md)).

## Alternatives
- **An idempotency-key store in write-api.** It would live in memory and be lost on restart, and it would not
  prevent stale overwrites.
- **Vector clocks or CRDTs.** They are more expressive, but more than one authoritative store needs.

## Consequences
- One durable comparison gives idempotent replay, protection from stale overwrites, and a result independent
  of arrival order.
- Ordering depends on the edges' clocks: fine on one host, but it needs NTP across hosts. A hybrid logical clock
  is the next step.
- Deletes need versioned tombstones, so v1 has none.
- **Evidence:** the chaos test kills an edge 200 ms into its drain; storage recognised every replayed write as
  a duplicate (59 in the final check).
