# D088 frontend task

Offline documentation-only correction. Do not edit source, build, deploy,
restart, contact the device, use credentials, or write Git.

Revise D086's frame-pacing plan using D087 findings: rate-limit the uncached
sentinel check to at most 1 Hz with an atomic cached value/timestamp, emit
the sentinel-armed acknowledgement on the 0→1 edge (including re-arm), and
state ordinary trace/sentinel cleanup. Preserve post-launch arming,
live-disarm latency, narrow write(2) output, registration, rollback, and
exact current line references. Do not apply the patch.

Write `docs/evidence/devin-d088-frontend-20261019.md` and
`coordination/_relay/devin-088-frontend-result.md`, ending exactly with:
`(handover to codex now)`
