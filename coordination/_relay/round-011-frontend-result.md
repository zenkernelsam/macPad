# Frontend R011 result — WS main-thread stall is not the coexist pacing wait

Read-only offline. No source/test/build edits, no build, no device/SSH, no Git
writes. Evidence: `docs/evidence/r011-ws-stall-symbolication-20261018.md`,
SHA `50193030110273682047d0ab0ff68117e1de49ca500c6f814f67a08199bc3975`,
plus the backend-copied sample.

## Inputs actually used (identity checked)

| Item | Value |
| --- | --- |
| spindump | `analysis/r007-ws-sample/ws-spindump-20261018.txt`, 3,911,680 B, SHA `de7fe9fb50e1018b0aa3e91eab98f4ee07ff9b862e3ee6cc99332f35129a3d05` |
| window | 2026-10-18 16:42:01.780–16:42:04.779, 150 samples ×20 ms |
| WS | PID 50574, UUID `465422C7-3CFC-3E9F-9C04-813F3A265AA6`, arm64 |
| cache | `BE59BB71-7B69-3EFC-86A8-5625A077AE2E`, slid base `0x19ba40000`, slide `0x1ba40000` |
| loaded images | `analysis/r007-ws-sample/loaded-images.json` SHA `dcff278eac75241f2732a90b4517f023d39481768a956b39a71e175da10794b2` |
| exact SkyLight | `analysis/SkyLight_22F82`, UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`, SHA `13d7a0ca010a9808234c71c34fd650b26329de99d1aceaeb5ddbb58f8b906c61` |
| installed libmachook | arm64e `9a511953…572819`, arm64 `151a2175…32bced` |
| local Theos build | arm64e `2df74403…562a`, arm64 `80b5ae2d…1152` — different, NOT used as a symbol substitute |

The spindump is root-first (entry at top, leaf at bottom): the CA thread's
bottom leaf is the turnstile sentence, not its top.

## Raw main-thread frames (150/150 samples; runtime addr, image+offset)

```text
0x19bae7f28  <983665BA…> + 2401042216
0x1028433bc  WindowServer + 13244
0x1a0e62504  _SLXServer+0x534                 (cache unslid 0x185422504)
0x1a0e626c4  _server_loop+0x8c                (0x1854226c4)
0x1a0e61bb8  _CGXRunOneServicesPass+0x57c     (0x185421bb8)
0x1a0e61f60  _post_port_data+0x118            (0x185421f60)
0x1a0e23258  _non_coalesced_timer_handler+0x18(0x1853e3258)
0x1a0e2314c  _run_timer_pass+0x1f8            (0x1853e314c)
0x102c5d098  <CC9C81E9…> + 0x39098            (injected dylib, unresolved)
0x1a0daf264  update_display_callback+0x558    (0x18536f264)
0x102c5cfd0  <CC9C81E9…> + 0x38fd0
0x1a0dbcbe8  WS::Updater::UpdateDisplays+0x33a0(0x18537cbe8)
0x1a0c9e210  SLCADisplay::present_update+0x374(0x18525e210)
0x1a0c9d000  SLCADisplay::finish_external_update+0x98 (0x18525d000)
0x1a6a66434  <983665BA…> + 2585060404        (unknown cache caller)
0x102c486b8  <CC9C81E9…> + 0x246b8
0x19be08834  libsystem_kernel +0x9834       (unslid 0x1803c8834)
0xfffffe0007f92f1c  kernel leaf, 146 samples  (nearest symbol assert_wait_deadline_with_leeway+0x1df4, approximate)
```

Other branches: 1 sample goes through `CC9C81E9 +0x245b4/+0x112a0` into
`IOWorkLoop::sleepGate+0x34` (`0xfffffe00085a3420`),
`IOCommandGate::runAction+0xcc` (`0xfffffe00085a7714`),
`lck_mtx_sleep+0xb8` (`0xfffffe0007f7fe88`); 3 samples end at kernel leaf
`0xfffffe0007f43388`.

## Determination (the only question asked)

**No sampled main-thread PC lies in
`macws_coexist_wait_for_completion_slot`'s poll/usleep.**

Pacing's call shape (root-first, leaf upward) must contain two adjacent
libmachook frames — an IOMFB begin / SwapEnd / IOConnect hook calling
`macws_coexist_wait_for_completion_slot` (`mac_hooks.m:27319`), then
poll/usleep. The observed near-leaf shape has a single injected-dylib frame
whose caller is either SkyLight display/timer (`run_timer_pass` →
`update_display_callback`) or an unknown cache image (`983665BA`), followed by
`libsystem_kernel` and a kernel wait. `CC9C81E9 +0x39098` is explicitly on the
display-callback path, not an IOMFB hook.

Classification: **SkyLight display-update/timer → kernel wait/lock**, with
**1 sample** an IOKit kernel lock (`IOWorkLoop`/`IOCommandGate`/`lck_mtx_sleep`)
and **146 samples** a kernel wait — not userland poll/usleep. The CA thread's
142/150 "blocked by turnstile waiting for WindowServer [50574] thread 0x978af"
only proves the main thread is the blocker; it is not used to infer pacing,
message starvation, or a provider cause.

## Exact limitations (no guessing)

- `983665BA-B173-392C-9874-65213940AEF1`: no path in the report;
  `loaded-images.json` base `0x10c918000` is inconsistent with the frame
  offsets (`0x8f1cff28` etc.), so it is not a valid mapping. The old lldb
  `CursorAsset` label is another generation and is not applied.
- `CC9C81E9-9998-336F-A191-C68162587D54`: no path; offsets
  `0x39098/0x38fd0/0x246b8/0x245b4/0xfde8/0x112a0` are inside an injected
  dylib whose exact UUID/bytes are not local. The installed libmachook hashes
  are known but have no local artifact, and the different local Theos build is
  not accepted as identity, so these frames cannot be proven to be installed
  libmachook or mapped to a named function.
- `libsystem_kernel +0x9834` (unslid `0x1803c8834`) is beyond the local 32 KB
  slice (`__TEXT` base `0x1803bf000`); the exact poll/usleep/psynch/mach_msg
  wrapper is therefore unproven (full image is on-device).
- Local kernelcache is T8103 iOS 16.3 `6673ce28…`; device kernel is
  8792.82.2. The 1-sample IOWorkLoop/lck_mtx symbols match; the 146-sample
  nearest symbol is adjacency, not identity.
- These limitations do not change the pacing verdict: the required two-frame
  IOMFB-hook shape is absent.

## Retained R009 responsibility result

Exact 22F82 libquarantine (`A2D17FF6…`, SHA `f5989c8f…`) has policy
`Quarantine`, selector `0xb6`, helper struct `{pid, pidversion, out-token ptr,
header ptr, x3=0}`. The stub GOT/bind (`0x1dfef8f88`) and the actual
103-producing branch (precheck / no-match default / matched handler) remain
unknown; this is not a proven no-policy result. No adjacent-version rework,
no setter implementation this round.

## Commands / exits

`shasum -a 256` inputs, `grep` main-thread/CA/image sections, Python
frame-address/slide bisect against SkyLight, Python bisect against local
kernelcache, `git log --oneline -1` (read-only): all exit 0. No tests, builds,
packages, device commands, or Git writes were run.

Last line:
(handover to codex now)
