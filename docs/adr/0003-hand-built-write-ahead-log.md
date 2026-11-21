# 0003. A hand-built write-ahead log

**Status:** accepted

## Context
Storage must not lose an acknowledged write, and each edge needs a durable queue for writes accepted during an
outage. Both need crash recovery.

## Decision
One write-ahead log in `common/wal`, used by storage and by every edge queue.
- **Records:** each has a magic number, a CRC-32 over its length and payload, a sequence number, the operation,
  the version, the writer id, the key and the value.
- **Writing:** appends are one unbuffered write, and the record is fsync'd before the append returns.
- **Recovery:** keep every valid record, skip a damaged record in the middle by resynchronising on the magic
  number, and cut off a torn tail.
- **Edge queues:** `discardThrough(seq)` removes exactly the replayed prefix, through a temporary file and an
  atomic rename.

## Alternatives
- **An embedded database** (SQLite, RocksDB). Robust, but it hides the very mechanisms this project
  demonstrates, and it adds a dependency to the hand-built core.
- **No fsync.** 6 ms writes instead of 114 ms, but an acknowledged write can vanish on power loss.

## Consequences
- **The project owns crash-safety code,** so it needed tests for torn tails, damaged headers, failed appends and
  concurrent appends.
- **The storage log is never compacted,** and start-up replays all of it (3.7 s for 300,000 records in a Debug
  build).
- **The fsync cost** led to group commit ([0007](0007-group-commit-without-window.md)).
