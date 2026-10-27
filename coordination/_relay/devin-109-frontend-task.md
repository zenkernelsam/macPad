# D109 — repair isolated diagnostic measurement invariants

Review D107 candidate issues, then fix only analysis/d107-pacing-candidate/
or a versioned analysis/d109 successor (no production application):

- last_check_ns load/access/store is not a multi-thread <=1Hz exclusion;
  simultaneous callers can stat concurrently and reorder cached state.
  Provide a nonblocking owner/CAS protocol, ack-before-events ordering and
  bounded rearm handling without changing pacing computation.
- parser keys only by seq; multiple processes/arms may reuse seq. Define
  identity and reject/segregate mismatched PID/TID/arm records; missing fields
  or malformed numeric values must not be accepted or crash parsing.
- correlate sums overlapping waits across threads/processes. Compute clipped
  union duration, scope by selected process/thread, and flag unknown thread
  linkage. RTT must be nonnegative, overlap <= RTT, residual >=0. Intersecting
  intervals are correlation only, not proven pacing-caused latency.
- full enter->exit includes pace_lock contention, trace I/O and pacing;
  distinguish wall interval from actual wait and identify observer overhead.

Add meaningful concurrent-arm and parser adversarial tests for these cases.
Keep exact baseline patch fingerprint; rerun apply-check, syntax checks and
registry generation verification. Diagnostic-only, no build/device/deploy/
trustcache/restart/source/Git changes. Save D109 evidence + handover.
