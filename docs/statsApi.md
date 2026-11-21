# Edge `/debug/stats` API

`GET /debug/stats` on every edge returns one JSON object describing that edge. The dashboard polls it every
second, and `EdgeServerTest.StatsMatchTheDocumentedSchema` reads the field table below and fails if any
listed field is missing or has a different JSON type. Change this table and the edge together.

Conventions:
- Timestamps (`…AtMs`, `atMs`) are milliseconds since the Unix epoch.
- Counters are cumulative since the edge started.
- `[]` marks the fields of every element of an array.

## Fields

| Field | Type | Meaning |
|---|---|---|
| `service` | string | Always `"edge"`. |
| `nodeId` | string | The edge's node id (`edge-1` …). |
| `uptimeMs` | number | Milliseconds since this edge process started serving. |
| `cache.size` | number | Entries currently cached (fresh and expired). |
| `cache.capacity` | number | Maximum entries before LRU eviction. |
| `cache.ttlMs` | number | Freshness lifetime of an entry; `0` means entries never expire. |
| `cache.staleIfErrorMs` | number | How long past its ttl an entry may still be served stale (`stale-if-error`). |
| `cache.hits` | number | Reads answered from a fresh cached entry. |
| `cache.misses` | number | Cache lookups that found nothing or an expired entry. |
| `cache.staleServes` | number | Reads answered from the cache with `X-Cache: STALE`. |
| `cache.revalidated` | number | Expired or `no-cache` reads the origin confirmed unchanged with `304` (`X-Cache: REVALIDATED`). |
| `cache.notModified` | number | Reads answered `304 Not Modified` because the client's `If-None-Match` matched. |
| `cache.purges` | number | `POST /purge/key/<k>` requests handled, whether or not the key was cached. |
| `cache.evictions` | number | Entries evicted to make room. |
| `cache.hitRate` | number | `hits / (hits + misses) × 100`, `0` before the first lookup. |
| `circuitBreaker.state` | string | `"CLOSED"`, `"OPEN"` or `"HALF_OPEN"`. |
| `circuitBreaker.consecutiveFailures` | number | Failed write-api calls/health checks since the last success. |
| `circuitBreaker.failureThreshold` | number | Consecutive failures that open the breaker. |
| `circuitBreaker.resetTimeoutMs` | number | Time OPEN before the single half-open probe. |
| `circuitBreaker.recentTransitions` | array | Up to the last 10 state changes, oldest first. |
| `circuitBreaker.recentTransitions[].atMs` | number | When the change happened. |
| `circuitBreaker.recentTransitions[].from` | string | Previous state. |
| `circuitBreaker.recentTransitions[].to` | string | New state. |
| `rateLimiter.capacity` | number | Token bucket capacity (burst) per client. |
| `rateLimiter.refillPerSec` | number | Tokens added per client per second. |
| `rateLimiter.trackedClients` | number | Clients that currently have a bucket. |
| `rateLimiter.callerTokensRemaining` | number | Tokens left in the bucket of the client making this stats request. |
| `rateLimiter.rejected` | number | Requests answered `429`. |
| `writeQueue.pending` | number | Writes queued in the edge WAL that write-api has not acknowledged yet. |
| `writeQueue.queued` | number | Writes ever accepted into the queue (`202`). |
| `writeQueue.reconciled` | number | Queued writes delivered to write-api by a drain. |
| `writeQueue.rejected` | number | Queued writes write-api refused with `4xx` (dropped). |
| `requests.total` | number | `/key` requests received (reads + writes). |
| `requests.reads` | number | `GET /key` requests. |
| `requests.writes` | number | `PUT /key` requests. |
| `events` | array | The latest 100 events, oldest first. |
| `events[].atMs` | number | When the event happened. |
| `events[].type` | string | `"breaker"`, `"reconcile"`, `"rateLimit"` or `"purge"`. |
| `events[].message` | string | Human-readable description. |

## Events

- `breaker`: one per breaker state change, for example `CLOSED -> OPEN`.
- `reconcile`: `started` and `finished` around every queue drain, with the counts.
- `rateLimit`: at most one per second per edge, with the number of `429`s since the previous rate-limit
  event and the last client rejected.
- `purge`: one per `POST /purge/key/<k>`, saying whether the key was evicted or was not cached.

## Example

```json
{
  "service": "edge",
  "nodeId": "edge-1",
  "uptimeMs": 81234,
  "cache": {"size": 3, "capacity": 1024, "ttlMs": 60000, "hits": 12, "misses": 4,
            "staleServes": 2, "evictions": 0, "hitRate": 75.0},
  "circuitBreaker": {"state": "CLOSED", "consecutiveFailures": 0, "failureThreshold": 3,
                     "resetTimeoutMs": 5000,
                     "recentTransitions": [{"atMs": 1795200000000, "from": "CLOSED", "to": "OPEN"}]},
  "rateLimiter": {"capacity": 200, "refillPerSec": 100.0, "trackedClients": 2,
                  "callerTokensRemaining": 199.4, "rejected": 0},
  "writeQueue": {"pending": 0, "queued": 1, "reconciled": 1, "rejected": 0},
  "requests": {"total": 19, "reads": 18, "writes": 1},
  "events": [{"atMs": 1795200000000, "type": "breaker", "message": "CLOSED -> OPEN"}]
}
```

The values above are an illustration of the shape, not a measurement.
