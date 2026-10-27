# D196 frontend result — 2026-10-20

Frozen D193 cleanup/execution-test review (hashes in report).
Report: `docs/evidence/devin-d196-frontend-20261020.md`.

## Verdict: ACCEPT (cleanup/decision/test sections)

- **Exact D192 mapping**: `d190_cleanup_class` (:153-161) reproduces
  the stub's four classes — SUCCESS+OK→NONE, SUCCESS+fail→DESTROY,
  {02,03,10}→NONE, else→DEALLOC. Mask domain {offsets 0,1,14}
  confirmed identical.
- **Execution, not labels**: counters increment inside the switch
  arms; p6 drives three real paths — valid→none, bad-id→destroy
  (real `mach_msg_destroy`), dead-remote→real `SEND_INVALID_DEST`→
  none (the class-3 regression), oversized→real `RCV_TOO_LARGE`→
  dealloc (real `mig_dealloc` on thread SRP). p7 pins the table for
  non-drivable constants.
- **Records preserved**: fill runs pre-classify; error paths keep
  verbatim kr in RET.aux, actual received id on malformed, msgid=0
  on dead remote — nothing fabricated.
- **Saved SRP** passed to dealloc (:209), not the received header —
  matches D192's defensive note.

Minor (non-blocking): complex-malformed replies are pure-table
covered only (no descriptor-bearing loopback injection) — destroy
execution proven via the non-complex arm; acceptable.

d190_main.c hash unchanged since D194 — F1/F2/F3 remain open under
backend's separate scanner/main repair; not inferred here.

Read-only; no edits/build/invocation/device; no commit.

(handover to codex now)
