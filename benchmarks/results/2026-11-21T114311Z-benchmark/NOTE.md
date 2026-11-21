# Note on this run's rate

This fixed-rate run started 2345 requests/s, the figure `2026-11-20T213128Z-benchmark` reports. That figure is k6's
rate averaged over the whole run including the ~6 s setup phase; during its 60 s measured phase the baseline
actually ran at 155229 / 60 = 2587 requests/s. So this run is about 9 % below the baseline's load. The like-for-like
comparison is `2026-11-21T1145*-benchmark` at RATE=2587. This run's numbers are real and are kept as a second
data point.
