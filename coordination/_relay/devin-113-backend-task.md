# D113 — bounded-memory diagnostic transport prototype

Use completed D112 contract to implement a concrete standalone transport
prototype under analysis/d113-trace-transport/, not libmachook or frontend's
unhanded candidate. Demonstrate recording while pace_lock context is active
does no filesystem syscall, no heap allocation, and never waits for a writer:
fixed-capacity preallocated record queue, bounded publish attempt; on full or
contention drop+count rather than block. Document single/multi-producer
assumptions accurately; do not silently block on mutex. Drain only outside
lock, validate opened regular file/uid/mode with nofollow and fd-bound writes,
preserve errno. Partial-write handling must invalidate/count dropped record.

Provide D111 adapter and execute completed D112 reference/negative controls
plus this prototype. Add bounded concurrent-producer saturation test and
writer-stall simulation proving producer completion independent of drain.
Include lost-record counters, process/thread/generation identity and shutdown
limits; never claim real-file O_NONBLOCK bounds storage latency. Candidate
production integration is NOT performed or accepted. Task-local C compilation
allowed; no frontend files, device, production markers/source/deploy/restart/
Git. Save actual outputs, honest gaps and explicit handover.
