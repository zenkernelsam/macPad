# Devin frontend D035 result — independent pinned-permit review

Date: 2026-10-18. Task: `devin-035-frontend-task.md`. Read-only; no
backend-file edits, no device, no builds. Disposable artifact:
`analysis/devin-d035-frontend/permit_audit.py`.
Evidence: `docs/evidence/devin-d035-permit-independent-review-20261018.md`.

## Verdict

**Mechanism approved; one P1 blocker before the A/B means anything.**

The v2 correction is right: all dynamic pins removed, every permit
argument independently recorded (IOMFB `7AC531B7…` table pin, 15.6.1
`{0}` fail-closed; QC set from `kMacWSQCFrameInfoABIs`), ordering
permit-before-install, fail-closed rollback, 4/32 budget, provider
contract untouched.

## P1 — `publicSwapEnd` cannot be patched by this trampoline

Its pinned prologue `b4000080 f9439401 b4000041 d61f083f` =
`cbz; ldr x1,[x0,#0x728]; cbz; br x1` — a branchy forwarder. The
provider refuses PC-relative/terminal instructions in the stolen window
(`cydiasubstrate_macos.m:482-485`), correctly — a verbatim thunk can't
relocate them. So: permit registers → `MSHookFunction` →
`REFUSED PC-relative/terminal` → SwapEnd still never installs →
cancel-completion never fires → no composite. The static checker
reproduces it; all 3 QC targets pass (`paciasp/stp` prologues).

Corollary: the Oct-12 `.bak` working provider had real branch
relocation (or was full CydiaSubstrate) — the scaffold lineage never
could install this hook; consistent with composite not returning after
Oct-12 even under the R002 repaired provider (THEORY).

**Smallest upstream fix — retarget to `kern_SwapEnd` @ img+0x4400**
(already pinned: +0x24 movW3 patch, +0x30 callsite check; stub forwards
args untouched → same interception; +0x24/+0x30 work lies outside the
stolen window). Missing inputs: (1) 16 bytes @ IOMFB-13.4+0x4400 —
classify then add to table; (2) evidence `[x0+0x728]`→kern_SwapEnd;
(3) live arm64-slice IOMFB LC_UUID (the pin may be the arm64e slice's —
bounded: -2 → named skip).

## P2/P3

- Install lines are sentinel-suppressed fprintf — production-silent
  either way. Add provider-side raw-write `INSTALLED image+off` for the
  install witness; until then use named-REFUSED deltas + state file +
  displayd counters + pixels.
- `macws_lc_uuid`/`macws_permit_for` Mach-O walks lack `cmdsize` bounds
  — mirror the removed walker's check.
- `MSG-REFUSED ×2` = `%hook NSScreen` (AppKit absent in WS) — noise, not
  a defect; `class=(null) sel=maximumPotential…` expected.
- 36 MSHook sites vs 4 permits = intentional; named refusals now
  enumerate the rest.

## Tests proposed

Static checker (delivered — catches consumer-migration gaps +
unrelocatable pins); host-side macosx provider harness (patch+
orig-callable positive, bad uuid/offset/prologue/branchy negatives —
reproduces P1 in-process); device A/B ladder with no fprintf dependency
(named-refused delta → CANCEL-COMPLETION → state `ready`+sequence →
displayd accepts → pixels); `--no-vnc` held identical to control.

Supplier lessons applied: layered witness, identity-before-reimplementation,
frozen-hash one-variable A/B, no bypasses, rollback claims only after
binary verification.

(handover to codex now)
