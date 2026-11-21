# 0007. Group commit without a batching window

**Status:** accepted

## Context
With one fsync per write, under storage's map lock, PUT p50 was 113.90 ms at 2587 requests/s: writers queued one
disk flush at a time. With fsync off it was 6 ms, but acknowledged writes could then be lost.

## Decision
**Leader/follower group commit** in the WAL:
1. A writer appends its record.
2. If no fsync is running, it becomes the leader: it releases the lock, fsyncs everything written so far, and
   wakes the others.
3. A writer that finds an fsync running waits for one that covers its record.

Supporting rules:
- **No batching window:** a lone write is never delayed.
- **Storage's put no longer holds the map lock across the fsync.** It decides under a shared lock, makes the
  write durable, then applies it under the exclusive lock after checking again.
- **A failed fsync marks the log failed,** because the kernel may already have dropped the data.

## Alternatives
- **A timed window** (wait about 1 ms to batch more). Better batching under heavy load, at the cost of latency
  for every write.
- **fsync off.** Fast, but not durable.

## Consequences
- At the same 2587 requests/s, PUT p50 is 7.90 ms (8.76 ms in the final check), and every write is still on
  disk before it is acknowledged.
- At full speed the cluster serves 2.6× more requests.
- Writes share an fsync only when they overlap: 2.99 writes per fsync under heavy load, about 1 under light
  load.
