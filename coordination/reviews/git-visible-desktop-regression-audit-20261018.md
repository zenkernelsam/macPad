# Reviewer Git audit — last visible desktop and regression candidate

## Proven visible baseline

`0614a5bb0b87ff223895bfbcda72d82293038f6e` (2026-10-13 02:00 HKT) adds `macws_publish_scanout_after_swap()` at delivered synthetic completion, plus displayd graph reconciliation. `0db21ae` (10-13 10:41) records actual runtime: workspace graph ready, wallpaper/Dock/menu bar ready, continuous final-composite sequence278+, framebuffer PNG showing Terminal/menu clock/Dock. Evidence: docs/evidence/ventura-22F82-input-transport-and-composite-20261013.md §§2–3.

Wallpaper static PNG substitution was DEVICE-ONLY in that evidence, not baked into rootfs builder. Therefore Git checkout alone cannot reproduce the complete old visual state; actual asset and deployed binary identities matter. Do not assert 0614a5b checkout is a complete known-good image.

## Strongest source-level regression candidate

`685624fc3035569584874cfb9ec2427b7bd0e46c` (10-14 22:22) adds global provider permission enforcement in `MSHookFunction`: unregistered UUID/offset/prologue targets return unpatched original. Current `MACWSHookPermitV2` production callsite search finds only the diagnostic CGXSYNTH registration under runtime-diagnostics opt-in. Existing IOMFB publicSwapEnd and other legacy hook callsites do not register matching permits. This is an integration defect in the hook permission contract: containment added globally without migrating existing required consumers.

Runtime corroboration reported in backend D033: current/previous generations zero frames, current21 REFUSED lines; historical .bak zero REFUSED and working hooks/composites. Today's installed provider and R010 backup both carry the gate, so R010 rollback is not known-good.

## Evidence limits / corrections to worker inference

- Provider refusals do not identify target addresses. A particular publicSwapEnd rejection is strongly supported by source contract, but not yet directly captured with UUID/offset/result/changed-bytes witness.
- `SwapEnd hook installed` is fprintf and may be suppressed without the chroot runtime diagnostics sentinel. Current source logs it unconditionally after MSHookFunction without verifying changed bytes. Its presence is not proof of installation; absence alone is not proof of refusal. This repeats a documented historical telemetry trap and must not be used as sole causal evidence.
- IOMFB is a distinct image from SkyLight; do not label all refusals SkyLight-internal or assume all21 are display targets.
- No controlled runtime A/B has yet established restoring a minimal permit restores pixels. Keep causal attribution provisional until target-specific evidence and functional witness.
- F11H failure is separately real; not proven sole reason for missing composites. Do not let isolated compiler work displace recovery of producer hooks.

## Next gate and reviewer responsibility

After current Devin tasks finish, request exact-target permission audit: UUID/prologue/offset, relocation safety, callsite policy and changed bytes for IOMFB completion/publisher chain, then minimal legitimate permit registration (not removing gate). Need owned-generation coordination with existing watchdog before any restart; D033 found an external watchdog-managed live generation, so never overwrite/start duplicate generations blindly. Preserve provider/PAC source and current baseline before one-variable change. Acceptance requires completed callbacks, advancing displayd composite sequence and visible wallpaper/Dock/menu bar pixels; process readiness/probe alone insufficient.

Reviewer process failure: provider probe/trust/package successes were accepted without rerunning the already-demonstrated desktop witness. Future global provider contract changes require legacy-consumer coverage and end-to-end regression check. No rollback or source/device mutation performed in this audit.
