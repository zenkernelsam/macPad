# D037 backend result — gated live A/B: PASSED (visible desktop restored)

Worker: macPad D037 backend 8057. Date: 2026-10-18.
Packet: `coordination/_relay/devin-037-backend-task.md` — real X-register/PAC
probe first, gated one controlled desktop A/B, then document + stop + handover.

## Gate 1 — PASSED

- Live identity from the running WS's `dyld_shared_cache_arm64e`
  (`task_for_pid` denied `kr=0x5`, cache extraction used instead):
  IOMFB `7AC531B7-05F5-38FE-92E5-DA6D86D22A14`, bytes @0x11cc
  `b4000080 f9439401 b4000041 d61f083f` — exact pin match;
  QuartzCore `CF853BBD-…` with all three RE'd prologues matching.
- `misc/test_mshook_relocate.c` as an iOS arm64e executable + iOS arm64e
  provider slice, run on device: **`RESULT: ALL PASS`**.
  - patch stub on the exact wrapper: `58000051 d61f0220`;
  - relocated `braaz` authenticated the IA+0-signed tail pointer and
    tail-called it (x0 preserved);
  - both cbz-taken paths returned via the continuation `ret`;
  - callable original (provider IA+0-signed per R012) replayed all paths;
  - refusals clean + named: b.cond, blrabz, internal-window cbz target,
    unpermitted target.
- Test-side fixes recorded in the evidence doc: PAC-signed function
  pointers must be `ptrauth_strip`ed before data use; trustcache input is
  `ldid -h` CDHash (the helper `cdhash.py` value was wrong); exec from
  `/private/var/tmp` is SIGKILL'd (use `/var/jb/tmp`); `launchdchrootexec`
  needs a real launchd job context.

## Gate 2 — controlled A/B: PASSED (all ladder rungs)

Deployed to the chroot after preserving originals:
`libmachook.dylib`←arm64e `f2143308`, `libmachook_arm64.dylib`←`bf3ae467`,
`System/.../CydiaSubstrate`←fat `82e74519` (all ldid-signed 2× + per-slice
trustcache; preserve set hashed in evidence doc).

`stop` → `start coexist --no-vnc --no-terminal --experimental
--runtime-cap=300` under a test lease:

1. Install: zero refusals for the 4 permitted targets (IOMFB SwapEnd,
   QC frame-info ×3) — SwapEnd's cbz window accepted by the relocator.
2. Counters: `replay-receiver-ready`, `replay-received` 1→7; displayd
   `final-composite-received producer=7763 surface=155 2732x2048`.
3. State: `state=ready producer=7763 sequence 9→…→60` (~190 s),
   `reason=validated-final-composite`.
4. Pixels: CARenderServer capture 2732x2048 `nonblack=0.984` — the
   captured frame shows the macOS desktop (menu bar, wallpaper, Dock)
   through MacWSHost after `uiopen macwshost://`. PNG artifacts saved:
   `docs/evidence/devin-d037-gated-live-desktop-20261018/frame-macos-desktop.png`
   (desktop) and `frame-pre-activation-ipados.png` (pre-activation
   negative control showing iPadOS WeChat).
5. Input: untouched this round (separate axis).

Crash delta: 3 ExcUserFault (known lightweight class). No watchdog/WS
instability; clean lease-gated stop afterward; iOS restored.

## Residuals

- replay-request replies report `source=unavailable published=NO` —
  RESOLVED by source analysis: `RememberReplaySource` is only called
  from `MacWSFinalCompositePublisherEnqueueCompletion` (the Metal
  command-buffer snapshot path), which is dead on this build because
  SkyLight `EndUpdate` never runs. The live path is SwapEnd completion →
  `macws_publish_scanout_after_swap` → owned-scanout IOSurface →
  `PublishSurface`, pushing on content-change + 4 s keepalive (also
  explains the ~0.3 fps event cadence the audit noted). Replay-pull is
  vestigial on this pipeline, not a delivery defect.
- Non-Z authenticated branch forms in stolen windows remain the
  documented bounded gap.
- iOS-side `/var/jb/Library/Frameworks` provider unchanged (no iOS
  permit consumers; fail-closed either way).

## Artifacts

- `docs/evidence/devin-d037-gated-live-desktop-20261018.md`
- `misc/test_mshook_relocate.c` (FPTR PAC-strip fixes + STEP tracing —
  kept; tracing is cheap and pinpoints future crashes)
- Deployment kept on device (acceptance passed → no rollback).
- No commit. No credential material persisted.

## Handover

The SwapEnd relocation + 4-permit chain is proven end to end on the real
device: permits match live image identity, the relocator emits and
executes correct arm64e/PAC thunk code, and the visible desktop is back.
Remaining axis: input path validation (separate dispatch), plus the
replay-request `published=NO` accounting question.

— end of D037 backend result; ready for coordinator review —
