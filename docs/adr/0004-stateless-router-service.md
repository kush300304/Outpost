# 0004. A separate, stateless router service

**Status:** accepted

## Context
Something has to map each key to its edge. Clients shouldn't need to know the ring, and the mapping must
survive restarts and edge failures.

## Decision
A dedicated `router` service, the only client entry point (port 8080):
- it builds the ring from configuration plus health checks, and persists nothing;
- an edge that fails two health checks, or refuses a connection, leaves the ring, and the request is retried
  once on the new owner;
- ownership doesn't depend on the order edges were added, so a restarted router routes exactly as before.

## Alternatives
- **Client-side hashing.** No extra hop, but every client must know the edge list and its health.
- **DNS or a generic load balancer.** No key affinity: every edge would cache every key, and hit rates drop.

## Consequences
- One extra network hop.
- The router is a single instance in v1. Because it is stateless and deterministic, several routers behind DNS
  would agree on every key.
- The edge list is static configuration: adding an edge needs a restart, and about 1/(N+1) of keys move.
- When an edge dies, the first request for one of its keys waits about 1 s for a bounded DNS lookup before
  failing over. A request already in flight to that edge fails.
