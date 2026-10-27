# D034 owned-generation A/B plan (permit-chain build)

Precondition: coordinator authorization; transient SSH creds; live watchdog
generation currently frameless (state=missing producer=0 sequence=0).

## Artifacts staged locally (theos, sha256)

- `libmachook/.theos/obj/arm64/libmachook.dylib`  bf3ae467…d4dc30  (D035 narrow permit set)
- `libmachook/.theos/obj/arm64e/libmachook.dylib` f2143308…f025c
- `CydiaSubstrateProvider/.theos/obj/macosx/CydiaSubstrate` fat 82e74519…e3a9
  (arm64 2a8da5bf…a8468b3, arm64e 3bde9239…d98855b) — D036 exact-relocation
  thunk builder: cbz/cbnz windows relocate via inverted-condition +
  absolute-branch pairs (external targets only — in-window targets
  refuse); b/bl/b.cond/tbz/adr/adrp/ldr-lit still refuse; blr/blra*
  refuse; terminal ret/br/braaz/brabz/eret copy verbatim and suppress
  branch-back. Contract unchanged (permit pin + fail-closed).
  Host thunk-byte verification: `misc/test_mshook_relocate_host.c`
  ALL PASS (emitted words byte-exact vs expected relocation).

## Step list (all bounded)

1. **Preserve**: hash + `cp -p` current on-device
   `/var/mnt/rootfs/usr/local/lib/libmachook.dylib` (arm64e),
   `libmachook_arm64.dylib` (arm64), and
   `/var/mnt/rootfs/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`
   into `/var/mobile/macws_d034_backup_20261018/`; record installed shas.
2. **Verify live image uuids** (once, read-only): in the running WS, confirm
   IOMFB LC_UUID via python3 task_for_pid walk of dyld_all_image_infos; abort
   plan if SkyLight uuid ≠ 96676A53 (would mean cache slice mismatch).
3. **Deploy**: scp arm64 slice → `libmachook_arm64.dylib`, arm64e slice →
   `libmachook.dylib`, fat provider → `CydiaSubstrate.framework/CydiaSubstrate`.
   (libmachook-only deployment is the stricter one-variable variant;
   recommend shipping the provider too since its only delta is named-refusal
   logging — needed to enumerate any residual refusals.)
4. **Watchdog handling**: the live generation is managed by
   `macos_gui.sh watchdog coexist` (pid 92972). Do NOT kill the watchdog
   blindly. Stop once via the launcher's own stop path
   (`macos_gui.sh stop coexist` semantics per prior rounds R012/R013);
   confirm WS/displayd/Host actually exit before starting.
5. **Start one owned generation**: same command shape as the visible baseline
   (`start coexist --no-vnc --no-terminal --experimental` equivalent —
   copy the exact argv from the watchdog's live args), bounded runtime.
6. **Acceptance ladder** (in order; stop at first failure). Log lines are
   hints only — acceptance requires the byte/counter/pixel witness each
   step names, not a `SwapEnd hook installed` fprintf alone:
   a. **Changed bytes**: read the 16 bytes at IOMFB img+0x11cc in the live
      WS (task_for_pid/pymacho read) — must equal
      `58000051 d61f0220` + `.quad <replacement>`; the QC prologues at
      their pinned offsets likewise. If the wrapper bytes are unchanged,
      the hook did NOT install regardless of any log line.
      Any residual `REFUSED` carries `image+offset sym=` — enumerate all.
   b. **Callback counters**: `CANCEL-COMPLETION delivered` counter
      advancing AND frame-info registration callback observed
      (`enable_tag_list` hooked → `macws_iomfb_complete_cancelled_swap`
      firing); `replay-receiver-ready` present.
   c. **State**: `/var/mnt/rootfs/private/tmp/macws_final_composite.state`
      → `state=ready`, nonzero producer id, sequence advancing across two
      reads ≥1s apart.
   d. **Pixels**: visible wallpaper/Dock/menu bar in MacWSHost — a real
      framebuffer readback/screenshot witness, not a Host-claimed state.
   e. Input untouched — separate axis; record `MSG-REFUSED` names for the
      input round.
7. **Cleanup**: keep generation running only if desktop visible; otherwise
   stop it and restore the preserved binaries. Report thermal + crash delta
   (iconservicesagent/MTLCompilerService baseline from D033).
8. **Evidence**: write `docs/evidence/devin-d034-device-ab-<date>.md` with
   raw log lines, state file contents, before/after hashes, PID/etimes,
   crash deltas. Do not claim readiness from process startup alone.

## Explicit non-goals

- No global permit bypass, no diagnostic-marker spoofing.
- No rollback to R010 or 0614a5b-era binaries.
- No compiler (R031) replay; no TCC/input changes this round.
- Do not disturb native SpringBoard/backboardd/tccd.
