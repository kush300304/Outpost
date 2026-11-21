# 0005. Consistent hashing with 150 virtual nodes per edge

**Status:** accepted

## Context
With `hash(key) % N`, adding or removing one edge remaps almost every key and empties every cache at once.

## Decision
- **The ring:** a consistent hash ring with 150 points per edge (`"edge-1#0"` … `"edge-1#149"`). A key belongs to
  the first point clockwise from its hash.
- **The hash:** FNV-1a, followed by the MurmurHash3 `fmix64` finalizer.
- **The structure:** points are `(hash, nodeId)` pairs in a sorted set, so ties break the same way every time.

## Alternatives
- **Rendezvous (highest-random-weight) hashing.** Equally good at moving few keys, but O(N) per lookup.
- **Fewer virtual nodes.** Less memory, but ownership becomes uneven.

## Consequences
- **Measured:**
  - plain FNV-1a gave a 10 / 56 / 34 % split, because similar strings landed near each other;
  - with the finalizer the split is 32.0 / 36.2 / 31.9 %;
  - adding a fourth edge moves 29.3 % of keys (ideal 25 %).
- Lookups are O(log points).
