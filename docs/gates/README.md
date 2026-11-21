# Gate records

Each phase of the project ended with an **exit gate**: a set of checks that had to pass before the next phase
started. These files keep the real output of those runs. Every number in the README traces back to one of them
or to `benchmarks/results/`.

| Record | Phase |
|---|---|
| `phaseA.md` | Data safety: WAL recovery, versioned writes, idempotent replay |
| `phaseB.md` | Failure detection: health that reflects storage, the circuit breaker, stale reads |
| `phaseC.md` | Wiring: the router service, the hash-ring fix, rate limiting, URL encoding |
| `phaseD.md` | Remaining v1 work: TTL, the stats contract, the event log, graceful shutdown |
| `phaseE.md` | Verification: sanitizers, compose, the chaos test, the first benchmark, CI |
| `phaseF.md` | The dashboard |
| `phaseG.md` | README and testing checklist |
| `phaseI.md` | Group commit |
| `phaseJ.md` | Standard HTTP caching and purge |

The final checks from a fresh clone are in `docs/finalCheck.md`.

**About references in these records:**
- **Branch names and commit hashes** (`v1-completion`, `b5a5d59`, …) point into the development history. That
  history was squashed into the v1.0.0 commit for publication, so the hashes are not in this repository.
- **"R1" … "R50"** are the findings of an initial code review, a private planning document. Each record says
  what the finding was where it matters.
- **"The owner"** is the project's author.
