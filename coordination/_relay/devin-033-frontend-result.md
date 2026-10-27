# Devin frontend D033 result — visible-desktop baseline audit

Date: 2026-10-18. Task: `coordination/_relay/devin-033-frontend-task.md`.
**Read-only local round** — no generation start/stop, no deploy/build/
commit, no device access, no marker/trustcache/source/Git changes.
Pre-existing uncommitted R012 PAC hunk in `libmachook/mac_hooks.m` left
untouched.

Full evidence: `docs/evidence/devin-d033-visible-baseline-audit-20261018.md`.
Sibling device inventory (backend D033, consulted):
`docs/evidence/devin-d033-device-regression-inventory-20261018.md`.

## Outcome

The visible desktop died at the **hook-provider link**, not in producer
code. On the last-rendered era the deployed CydiaSubstrate installed the
full WindowServer hook surface (Oct-12 `WindowServer.err.bak`: `SwapEnd
hook installed` ×10, `replay-receiver-ready`, 654 CANCEL-COMPLETION,
REFUSED=0). Since the provider transition (bounded post-Oct-12 /
pre-Oct-14 scaffold discovery; permit-hardened builds ever since),
**every `MSHookFunction` target is refused in production** (zero permits:
the sole `MACWSHookPermitV2` call site is the marker-gated CGXSYNTH diag
gate) → SwapEnd hook never installs → no cancel-completion → no
`macws_publish_scanout_after_swap` / `macws_publish_graphics_ready` →
no `MarkContentValidated`/`PublishSurface` → replay receiver never
starts → `state=missing producer=0 sequence=0`.

libmachook producer code is **byte-identical** to the 10-13 baseline
(`git diff c48a81d..HEAD` on `Metal_hooks.x`/`mac_hooks.m` = empty except
TCC service map + CGXSYNTH permit + R012 PAC fix). R008/R010/R012/R013
all share the refused-provider regime and all produced zero composites —
the regression predates them.

## Milestone evidence

| milestone | evidence | status |
|---|---|---|
| 10-13 baseline reconstructed | source `b1f9bb9`/`3cb8e21` era; producer commits `c48a81d`+`0614a5b`; launch `start coexist` (VNC); provider = working pre-scaffold binary (identity GAP); libmachook deploy SHA GAP; wallpaper = device-side `DefaultDesktop.heic` PNG substitution (hash GAP) | done, gaps itemized |
| producer chain proven | source table in audit §3: SwapEnd MSHook `mac_hooks.m:6290-6351` → registration `:19804-19863`/`:21609-21616` → publish `:21690-21699` → `MarkContentValidated`/`PublishSurface` `MacWSFinalCompositePublisher.m:187-205` → replay `:736-780` | done |
| suppressor identified | permit-gated provider refuses all targets (FACT); sentinel absence kills only the redundant didComplete swizzle path (secondary); `--no-vnc` REJECTED as suppressor (gates RFB bridge only; bypasses the unwinnable graphics_ready gate — symptom mask, not cause) | done |
| R008–R013 compared | identical regime+outcome; R010 baseline provider `7320a210…` also permit-gated → **not** a known-good | done |
| artifact inventory | `.old/.old2/.old3` provider backups on device unverified; R002 `4841131` build hash unrecorded; identifier reconciliation needed (`DFF425E9`/`EC16F01C`/`1d3c525a`/`7320a210`) | itemized |
| A/B plan | one variable = `MACWSHookPermitV2` registrations for the working-era targets (min: SwapEnd target + `enable_frame_info_tag_list`, offsets already pinned); readout ladder install→CANCEL-COMPLETION→replay→state=ready→pixels; reversion = restore `b2ae21da…` | proposed, not executed |

## Cross-check vs backend inventory

Consistent and complementary: backend proved on-device that all three
post-19:30 generations delivered zero frames with REFUSED=21–58 and that
the Oct-12 `.bak` had the full working hook surface. This audit adds the
source-side proof that the refused SwapEnd/registration links are the
exact chain the 10-13 desktop rode, and that no libmachook delta since
explains it.

## Limits

Local read-only; on-device facts cited from the backend transcript.
Working-era provider binary unpreserved (`.old*` candidates unhashed).
Oct-12 virtual-display registration vs the bit-gate analysis: noted
observation, resolves on re-hook. F11H/MTLCompiler decode aborts =
separate class, likely next blocker after transport is restored.

(handover to codex now)
