# Devin backend D035 — narrow independently-pinned display permits

Scope honored: local source+build only; no device deploy/start/stop; no
commit/package; stopping after this handover.

## Correction applied

D034's dynamic pins removed. Final permit set = 4 essential publisher-chain
targets, every argument independently recorded:

- IOMFB `publicSwapEnd` @+0x11cc — uuid `7AC531B7-05F5-38FE-92E5-DA6D86D22A14`
  (otool LC_UUID on the extracted dscout_ventura image), prologue = ABI table
  wrapper sig `b4000080 f9439401 b4000041 d61f083f`. 15.6.1 row uuid zeroed →
  fail closed.
- QC `enable_frame_info_tag_list` @+0x29285c + begin/finish pacing
  @+0x291288/+0x291220 — all pinned by the existing `kMacWSQCFrameInfoABIs`
  13.4 row (uuid CF853BBD…, RE prologues).

Removed: `macws_sl_hook_permitted`, all 8 SkyLight gates (call sites restored
to unpermitted attempts → provider fails closed and now names the target),
the live-uuid reader/self-pin path. Provider gate unchanged; named-refusal
telemetry kept as bounded diagnostic only.

## Builds

- libmachook arm64 `bf3ae4675d7f…dc30`, arm64e `f21433080c71…9146`
- provider fat `694be5a2…d41d2d` (unchanged — log-only delta)
- `git diff --check` clean; R012 PAC hunk untouched.

## Remaining risks (precise)

1. IOMFB uuid may be the arm64e-slice uuid while WS could load arm64 —
   outcome is bounded: permit -2 → skip + named log; either way the A/B
   reports it. To pin it exactly: one read of the live WS image LC_UUID.
2. SkyLight tolerate-nil/completion wrappers stay refused this round — if the
   4-permit generation shows completions but no pixels, they are the next
   pinned set (fallback tables already carry RE'd 12-word signatures; first 4
   words + uuid 96676A53 + table offsets are ready).
3. `MSG-REFUSED bad args` swizzle failures are a separate defect, now named.

## Revised plan note

Generation plan (`devin-034-owned-generation-plan.md`) stands; amend step 3:
deploy libmachook only for the strictest code A/B; provider deploy optional
telemetry A/B. Watchdog (pid 92972-era) must be stopped via its own stop path
with exit verified before any owned start — no duplicate WS.

Evidence: `docs/evidence/devin-d035-pinned-display-permits-20261018.md`.

(handover to codex now)
