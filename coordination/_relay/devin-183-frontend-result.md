# D183 frontend result — 2026-10-20

Memory-model audit of D180 UUID accessor contract.
Report: `docs/evidence/devin-d183-frontend-20261020.md`.

## Verdict: flag design correct; one real defect

`g_uuid_valid` release/acquire publication is C11-correct — torn
reads impossible on first set; zeroed-out, errno-free, off emit
path all verified.

**F1 — re-set gap**: `set_generation` rolls `g_state` back to UNINIT
(:70) — the same state its own CAS claims — so a second call can
legally re-enter INITIALIZING and overwrite uuid/pid/gen while
`g_uuid_valid` stays 1 → `get_uuid` returns torn bytes of two UUIDs
as valid. My "immutable" claim was wrong about current mechanism.

## Fix

Once-check inside the CAS-won window: `uuid_valid → rollback UNINIT,
return -1` — restores first-set-wins as enforced property, no new
globals, zero torn windows. Changes a legal-but-unused edge
(re-set was already downstream-dead via E_MULTIGEN). Weaker
alternative (clear+republish per window) rejected — still lies
about "the" generation.

U6 spec corrected (works pre-gate only with the fix); added
concurrent-re-set assertion to U9/U6 via existing `hook_gen_init`.

Read-only — no edits/builds/device/integration. No commit.

(handover to codex now)
