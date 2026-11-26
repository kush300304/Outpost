# Trial run before the TCP_NODELAY fix

Measured at commit `f9b464d`, before `2f9c7c8` disabled Nagle's algorithm. Every request after the
first on a keep-alive connection waited ~40 ms for a delayed ACK, which is why even cache hits show
a p50 of ~44 ms here. Kept as the measurement that exposed the problem; superseded by later runs.
The status-count line was not captured by this version of the script.
