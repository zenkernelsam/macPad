# D167 frontend result — 2026-10-20

Serialize-one-probe feasibility audited.
Report: `docs/evidence/devin-d167-frontend-20261020.md`.

## Verdict

**No current tool sends `0x74a7`** — `macos_ws_event_probe` posts
`_XPostEventRecord` (rc=1002 path); `0x74a7` is
`_XGetWindowServerTimebase` (D100). Serialization is vacuous without
a sender — that's the blocking integration item, not the rule.

## If a sender existed

- One synchronous `SLSPostEventRecord`-style call = one-in-flight by
  construction (no threading/async in probe source).
- ≥30 serial samples via sequential process invocations = strictest
  serialization (each a fresh sender — harmless; rule is temporal).
- Retries/duplicates: invisible to the CLI (rc-only); detectable only
  on-record → >1 `CALL(0x74a7)` in a window voids the sample span.
- Timeout: `RET(0, MACH_RCV_TIMED_OUT)` fails closed via the D162
  predicate — unmatched CALL, flagged, never corrupt.

## Fail-closed scoring rule (proposed)

One CALL per window; ordered `CALL→HANDLER_BEGIN→REPLY_ENQUEUE→RET`
on seq-contiguous records; non-success RET fails the sample; ≥30
valid/arm with invalid samples reported. Quiescence is a
load-bearing precondition (identical-key ambient sends are
indistinguishable — no port field).

Cross-process ring merge noted as an existing §9 integration gap
(shared CLOCK_MONOTONIC domain makes timestamp-merge feasible).

Read-only — no code/build/device/integration. No commit.

(handover to codex now)
