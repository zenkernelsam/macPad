# D159 frontend result — 2026-10-20

T16 global-hook sequencing audited and corrected in the packet.
Report: `docs/evidence/devin-d159-frontend-20261020.md`.

## Defect fixed

`d141_hook_post_claim` fires after EVERY successful claim
(`d141_svctime.c:113`). D158's T16 left it armed through the fill —
all 255 step-4 emits would have entered `pause_hook` and deadlocked
the test. Corrected order: arm → P parks → `hook_entered` → clear
pointer (P stays blocked in its in-flight callback on `hook_gate`)
→ drain/fill (hook NULL) → refusal → flood → `hook_gate` release →
final drain. One-claimant constraint while armed stated explicitly.
T9/T12's clear-then-fill pattern cited (`:185-203`, `:283-299`).

D158 arithmetic and all earlier gates preserved; revision map
extended.

Docs only — no source/test edits, no builds, no device, no backend
contact. No commit.

(handover to codex now)
