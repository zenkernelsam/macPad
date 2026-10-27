# Devin frontend D034 result — independent audit of the permit-chain change

Date: 2026-10-18 (continuation after D033 handover). Scope: read-only
review of the uncommitted backend change set + the owned-generation plan.
No device access, no edits, no builds. Note: the change was revised
mid-review (12-permit v1 → 4-permit minimum v2); verdict covers v2.

Evidence: `docs/evidence/devin-d034-permit-audit-frontend-20261018.md`.

## Verdict

**Approve for the bounded owned-generation A/B.** All four registration
sites (IOMFB `publicSwapEnd`, QC `enable_frame_info_tag_list`, QC pacing
begin/finish) register pinned (uuid,offset,prologue) permits after their
existing signature verification and before `MSHookFunction`; refusals
fail closed with named logs; `_Atomic installed` flags reset for retry;
permit budget 4+1 of 32. Provider named-refusal logging is safe (raw
write, stripped symbol for dladdr, caller pointer for display). No
symptom-suppression patterns — the change authorizes through the
contract rather than bypassing it.

v2 is strictly better than v1: table-pinned IOMFB uuid
(`7AC531B7…` Ventura; `{0}` fail-closed on 15.6.1) instead of live
self-pinning, and the live-prologue SkyLight registration (no added
verification + arm64e PAC-read caveat) removed.

## Findings for the deployer

1. **Coupling**: new libmachook requires a V2 provider (installed
   `b2ae21da…` qualifies). Never pair new libmachook with a pre-V2
   provider — every site skips. Strictest one-variable test is
   libmachook-only; ship the new provider too for named refusals.
2. **Doc staleness**: D034 doc records v1 hashes/sites. v2 objects:
   arm64 `bf3ae467…dc30`, arm64e `f2143308…9146`, provider
   `694be5a2…d41d2d` (unchanged). Re-record at deploy.
3. **SkyLight set deferred**: tolerated-nil/composite hooks remain
   refused — WS survives without them (21 min+ live), but pixels may
   still fail if the nil-wrap path is hit. Watch composite-nil/assert
   lines; the 8-site permit list is the queued next increment.
4. **Plan refinements**: preflight should compare live IOMFB uuid to the
   `7AC531B7…` pin (and QC `CF853BBD…`), not only SkyLight; restate
   deploy mechanics (fresh inode, `ldid -S/path`, trustcache refresh,
   platform marker); diff remaining named `REFUSED` lines vs the
   R013-era 21 as a negative witness; keep `--no-vnc` identical to
   control.
5. **MSG-REFUSED ×2 attribution resolved**: the two `bad args` refusals
   are `%hook NSScreen`'s two methods — NSScreen is AppKit-only and WS
   doesn't link AppKit → `MSHookMessageEx(NULL,…)` ×2. Not a defect;
   expect `class=(null) sel=maximumPotential…` under the new named log.
   The QC-DESKTOP-TARGET/METAL-TARGET swizzles use
   `class_addMethod`/`method_setImplementation` — provider-independent,
   unaffected by the refusal regime (they skip silently on missing
   selectors behind marker-gated logs).

## Limits

Local audit only; IOMFB uuid pin verifiable only by fail-closed behavior
(no local binary to re-hash); the mid-review v1→v2 revision means the
backend's D034 doc and this verdict cover different trees — update the
doc before deploy.

(handover to codex now)
