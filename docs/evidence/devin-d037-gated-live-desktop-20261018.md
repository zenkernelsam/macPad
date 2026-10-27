# D037 — Gated live device round: arm64e/PAC relocation witness + controlled desktop A/B — PASSED

Date: 2026-10-18. Task: `coordination/_relay/devin-037-backend-task.md`.
Worker: macPad D037 backend 8057. Device: iPad13,11 / 20D47, iPadOS 16.3,
Dopamine rootless, Ventura 13.4 (22F82) chroot.

Scope executed: (1) real X-register/PAC probe on arm64e; (2) gate-authorized
controlled desktop generation A/B; (3) documentation + handover. No commit.

## Gate 1 — live identity + arm64e PAC probe

### Live image identity (read at runtime)

Direct `task_for_pid(WS)` denied (`kr=0x5`), so identity was verified by
extracting the images from the live `dyld_shared_cache_arm64e` that the
running WindowServer maps:

- `IOMobileFramebuffer` uuid `7AC531B7-05F5-38FE-92E5-DA6D86D22A14`,
  cputype arm64e; bytes at image offset `0x11cc` =
  `b4000080 f9439401 b4000041 d61f083f`
  (`cbz x0,+0x10; ldr x1,[x0,#0x728]; cbz x1,+8; braaz x1`) — exact permit pin match.
- `QuartzCore` uuid `CF853BBD-01B6-3F46-ADA1-EC70FD2DC9DC`;
  `0x29285c`: `d503237f d10243ff a9036ffc a90467fa`;
  `0x291288`/`0x291220`: `d503237f a9be4ff4 a9017bfd 910043fd` — all match pins.

### arm64e live-execution witness (RESULT: ALL PASS)

`misc/test_mshook_relocate.c` built as an iOS arm64e executable
(`-target arm64e-apple-ios16.0`) plus an iOS arm64e slice of the provider
(same source as production). Executed on device; the relocator ran against
the exact real IOMFB wrapper words:

```
arch=arm64e (PAC path active)
shim-before: b4000080 f9439401 b4000041 d61f083f
shim-after : 58000051 d61f0220        ; ldr x17,#8 / br x17 patch stub
signed-impl=0xb84d0001040dcdb0        ; IA+0-signed tail pointer
hooked=3 tail=2
RESULT: ALL PASS
```

Proven on real hardware/PAC:
- cbz taken path (null fb) → continuation `ret`, no tail.
- cbz taken path (null impl) → second cbz taken → ret.
- signed impl → relocated `braaz` authenticated the IA+0 pointer and
  tail-called `tail_target`; x0 arrived intact (`g_tail_arg == fb`).
- Callable original (`g_orig`, provider-signed IA+0 per R012) replayed all
  three paths correctly (`tail` incremented to 2, hooked=3 unchanged).
- Refusals named + clean (untouched bytes, callable orig): `b.cond`
  (unrelocatable PC-relative), `blrabz` (indirect call), internal-window
  cbz target, unpermitted target.

### Probe logistics (recorded for the next round)

- `cdhash.py` produced a hash that did NOT match `ldid -h` output; the
  kernel validates ldid's `CDHash=` value — use `ldid -h` (or `-arch X -h`
  per slice) as the trustcache input. Wrong-hash trustcache = silent SIGKILL.
- Executables in `/private/var/tmp` are SIGKILL'd at exec (launch
  constraint on the data volume); `/var/jb/tmp` execs fine.
- `launchdchrootexec` from SSH dies (SIGKILL): the iOS-side exec path is
  fine but the posix_spawn target hits AMFI without a real launchd job
  context (`XPC_SERVICE_NAME` + launchd parent). A launchd plist job works.
- arm64e C code must `ptrauth_strip` function addresses before using them
  as data pointers / image-offset bases — `&fn` is IA+0-signed; arithmetic
  or memcpy on it produces wild addresses (probe initially SIGSEGV'd on
  exactly this; fixed via `FPTR()`).

## Gate 2 — controlled generation A/B

### Deployment

Preserved originals (`/var/mnt/rootfs/tmp/d037/preserve/`):

| file | preserved sha256 |
|---|---|
| libmachook.dylib (arm64e) | `47f78880f6de1fadc9faabbf79d16ccc56cfba2feb5a45e2155e09c22c2d9432` |
| libmachook_arm64.dylib | `6de9b6880495c3e0f431c2fc0e328b4420aecac178391d466fa71b141efcbd18` |
| CydiaSubstrate (fat) | `b2ae21da42704b1d584f2b655e837f7db62a10f2a035b3f741adc819ce1423e7` |

Installed (recipe per `build_on_ios.sh`: `ldid -S<entitlements.plist> -M`
twice per file, `jbctl trustcache add` per-slice cdhash, `rm`+`cp` to avoid
the inode landmine):

| target | installed (signed) sha256 | source artifact |
|---|---|---|
| `/var/mnt/rootfs/usr/local/lib/libmachook.dylib` | `aa041efda7a33099347c5d51865be82c1dd9004b782bea57252df4679e7d1113` | arm64e slice `f2143308…9146` |
| `…/libmachook_arm64.dylib` | `21f81b5cbc8c19fcc3e531d78f6bf220286f5545477baccb9225b89ad9b9b39b` | arm64 slice `bf3ae467…dc30` |
| `…/CydiaSubstrate.framework/CydiaSubstrate` | `134b7eb954ddfb65ca746c471fcad6d831c79745c48b9701ef74ef03dbcc85e3` | fat `82e74519…e3a9` |

Test lease held for the round (`/tmp/macws_test_lease`, token-gated
start/stop); released after teardown.

### Generation

`macos_gui.sh stop` (clean, iOS restored) → `start coexist --no-vnc
--no-terminal --experimental --runtime-cap=300`. Watchdog armed, thermal
nominal (3269 centi-°C).

### Acceptance ladder — ALL RUNGS PASSED

1. **Install**: no `REFUSED` for IOMobileFramebuffer or the three
   QuartzCore frame-info targets — all four D035 permits matched live
   (uuid+offset+prologue). Remaining named refusals are the intentional
   diagnostic/experimental sites (SkyLight composite block, featureflags,
   XPC shims) — all enumerated with image+offset+symbol.
2. **Counters**: `#### FINAL-COMPOSITE replay-receiver-ready` +
   `replay-received count=1..7`; displayd logged
   `final-composite-received producer=7763 sequence=1 surface=155
   size=2732x2048 bpr=11008` — a real IOSurface delivered end to end.
3. **State**: `macws_final_composite.state` =
   `state=ready producer=7763 sequence=9→11→32→46→58→60` across reads
   spanning the generation (~190 s wall time);
   `reason=validated-final-composite`.
4. **Pixels**: `macws_ipados_capture` (CARenderServer, read-only):
   `pixels=2732x2048 nonblack=0.984 mean=107.5` after
   `uiopen macwshost://`. The captured frame shows the macOS desktop —
   Finder menu bar, Ventura forest wallpaper, macOS Dock — presented via
   MacWSHost. displayd's `workspace-graph-state` confirms
   `wallpaper=ready dock=ready menubar=ready backing=ready`.
   (An earlier capture before Host activation showed iPadOS WeChat —
   the desktop lives in the MacWSHost scene; `subscribers=0` in displayd
   before activation.)

   Saved artifacts (this directory):
   - `frame-macos-desktop.png` — desktop capture, sha256
     `1d8ff47986c61f9fced3edd0772d8d9590ef3e751cae5245f224301c57c558e2`
   - `frame-pre-activation-ipados.png` — pre-activation negative control
     (iPadOS WeChat), sha256
     `6bfd1827862df3e3a471a5639cad1f97061a31987da890fbfcf61dd67bed0640`
5. **Input**: separate axis — not exercised this round (by design).

### Crash / health delta

3 new `ExcUserFault` reports during the generation (pbs,
hiservices-xpcservice, filecoordinationd) — the known lightweight fault
class, same as baseline generations. No WS/displayd/Host crashes; no
thermal trips; watchdog armed normally, `runtime cap=300s` recorded.

### Teardown

`macos_gui.sh stop` via lease token: generation torn down cleanly, iOS
SpringBoard restored. Composite state transitioned to
`state=fallback reason=layer-topology` during shutdown (expected).
Deployment KEPT — acceptance passed, so no rollback. Probe leftovers
(`com.macwsguide.d037probe.plist`) removed; lease file removed.

## Residuals (documented, non-blocking)

- Replay-REQUEST responses report `source-completion=0
  source=unavailable published=NO`. RESOLVED by source analysis (not a
  transport defect): `RememberReplaySource` is only invoked inside
  `MacWSFinalCompositePublisherEnqueueCompletion`
  (`MacWSFinalCompositePublisher.m:820,876`) — the Metal command-buffer
  snapshot path — which is dead on this build because SkyLight's
  `EndUpdate` never runs on the virtual-display pipeline (documented at
  `mac_hooks.m:21754` area). The live publish path is
  `SwapEnd completion → macws_publish_scanout_after_swap`
  (`Metal_hooks.x:8348`) → latest owned-scanout IOSurface → CPU
  pixel-sample validation → `PublishSurface` (pushes on content change
  plus a 4 s keepalive — which also explains the event-driven cadence).
  The replay-pull protocol is therefore vestigial on this pipeline;
  reconnecting it would require teaching the scanout path to remember a
  Metal queue+texture source, which the pure-CPU IOSurface path does not
  naturally own.
- Non-Z authenticated branch forms (`braa/brab/blraa/blrab`, `0xd7xx`) in
  a stolen window remain a bounded gap: copied verbatim + dead
  branch-back. No current target uses them.
- iOS-side provider at `/var/jb/Library/Frameworks/…` was left at its
  prior build (chroot path carries the new build; iOS consumers register
  no permits and keep fail-closed semantics either way).
