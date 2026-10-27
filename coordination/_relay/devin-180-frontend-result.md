# D180 frontend result — 2026-10-20

Isolated D141 full-UUID milestone acceptance plan frozen.
Report: `docs/evidence/devin-d180-frontend-20261020.md`.

## Plan skeleton

- Delta: `g_uuid[16]` + `g_uuid_valid` release/acquire flag inside
  `set_generation`'s INITIALIZING window (uuid stored first, flag
  last — mid-window readers can't see a torn uuid, which relaxed
  pid/gen stores could NOT prevent); read-only `d141_get_uuid`
  (`1 → verbatim +0`, `0 → zeroed +−1`); reset clears both.
- Semantics: first-set immutability, valid-set→reset domain,
  empty→`-1`+zeroed, disarmed gate orthogonal, lo64↔record binding
  proven in-test.
- Tests U1–U8: round-trip incl. hi64, unset `-1`, record-binding
  consistency, reset, deterministic publication race via existing
  `hook_gen_init` (no sleeps), immutability, errno, 437-check
  regression.
- Acceptance R1–R4: sanitizer runs + deterministic gate proof +
  provenance-not-verifiability contract wording + additive-only API.

## Explicitly separated

This improves header provenance only — records carry gen/pid;
hi64 is never record-verifiable; decoder treats it as
producer-declared (D175 wording). No consumer/hooks/sender/
transport/device/frame-wait claim. Commit only after review.

Read-only design — no edits/builds/device/integration. No commit.

(handover to codex now)
