# D164 frontend result — 2026-10-20

Paired service-wait measurement plan derived from accepted D150b
(`6d5f2e2`, offline-only) + D138/D140. Report:
`docs/evidence/devin-d164-frontend-20261020.md`.

## Plan skeleton

- Two arms minimum: wait-free baseline vs one bounded wait interval;
  ≥30 `0x74a7` probes/arm, same generation/payload/thermal, cold-start
  per arm; score = median `HANDLER_BEGIN − MACH_CALL` on measured
  services tid; cluster threshold `max(8ms, 0.5×interval)`.
- Join keys: `gen` tag per generation, measured services `tid`, raw
  msgid + decoder `id−100` reply correlation, nearest-unmatched
  same-tid leg pairing.
- Mach semantics per accepted contract: pre-call send snapshot,
  RET msgid only on SUCCESS&&RCV else 0, non-success RET excluded
  (D162 predicate), TOO_LARGE→0, combined legs pair by order not
  msgid, no 40000–41000 filter inheritance.
- Pass bracket = batch; handled count derived by edge-counting inside
  the bracket (I4 route, PASS_EXIT aux=0).
- Drops: `record.seq` gaps = never-reached-ring; stall =
  CAP-bound honest drop; nonzero-drop arms flagged.

## Explicitly separated

D150b measures nothing in production yet — missing pieces table
(permits, hook installation, sentinel path, drain transport,
D8 enforcement, in-situ calibration, decoder, device scheduling)
each gated on a separately reviewed integration milestone.

## Accepted-evidence state summary

Provider mapped (REFUSED witness), real trampoline packaged
(`b717fb9`), positive permit install still inferred; F1 `1002` open,
reopen-freshness never exercised; D135 PNG closed L5 for that
generation only.

Design only — no code/build/device/sentinel/integration. No commit.

(handover to codex now)
