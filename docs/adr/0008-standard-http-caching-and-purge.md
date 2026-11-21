# 0008. Standard HTTP caching, with the write identity as ETag, and purge

**Status:** accepted

## Context
Clients and other caches should be able to check whether their copy is current without downloading it again.
Operators need to remove a key from every edge immediately. Custom headers would work only with custom clients.

## Decision
- **Headers:** every answer carrying a value has `ETag: "<version>-<writerId>"` (the write identity from
  [0002](0002-versioned-last-writer-wins.md)), `Last-Modified`, `Age`, and
  `Cache-Control: max-age=<remaining ttl>, stale-if-error=<s>`.
- **Conditional reads:** a matching `If-None-Match` is answered `304` with no body.
- **Revalidation:** an expired entry, or one requested with `no-cache`, is revalidated with the origin, and
  write-api and storage answer `304` if nothing changed.
- **Bounded staleness:** a stale copy is served for at most ttl + `stale-if-error`.
- **Purge:** `POST /purge/key/<k>` on the router goes to every configured edge, including one that is off the
  ring. The answer is `502` with `complete: false` if any edge could not be reached.

## Alternatives
- **Hash the body for the ETag.** It costs CPU on every read and adds nothing over the write identity.
- **Custom headers only.** Browsers, CDNs and HTTP libraries couldn't use them.
- **Purge only the ring.** An edge briefly off the ring could serve the old value when it rejoins.

## Consequences
- **Measured:** 77.4 % of conditional reads answered 304 at 1 % writes (26.2 % at 10 %), each saving a
  4126-byte body. That is about 1.39 GB not sent in a 60 s run.
- **Writer ids** are restricted to `[A-Za-z0-9._-]`, because they appear in a header.
- **Purge limits:** it removes one key at a time and is not a barrier. A cache fill already in flight can bring
  the old value back until the TTL.
