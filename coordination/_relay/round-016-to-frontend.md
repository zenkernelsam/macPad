# Frontend R016 — audit existing reopen freshness contract for next acceptance

The project already has application reopen freshness logic. Inspect the exact
`macwshostd/main.m` implementation around the existing application-reopen
and previous/current metrics-generation checks (reported around lines
1654–1685), its protocol/data producer, focused tests and any existing runtime
evidence. Do not rebuild or duplicate the mechanism.

Produce a read-only audit that separates source/test-confirmed behavior from
device runtime acceptance. Give the backend a bounded next-window witness:
which app/process identity to reopen, how the current request/response is
correlated, how a fresh metrics generation is proven to belong to this reopen,
and what exact logs/files/counters establish success. State the failure rule:
stale metrics or missing response is not success, and document-bearing
processes remain open on failure. Avoid adding a bundle-ID exception or
changing lifecycle code.

No device/SSH, source/test/build-input edits, builds, dependency installs or
Git writes. Write only a dated evidence note and
`coordination/_relay/round-016-frontend-result.md`. End exactly:
`(handover to codex now)`.
