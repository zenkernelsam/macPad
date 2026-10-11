# Handover: resume MacWS Ventura bring-up from an x86 macOS dev host

Date: 2026-10-11
Author: Devin session handover (from Apple-Silicon Mac dev host → x86 Hackintosh)
Scope: this document plus the companion `macpad-gitignored-*.tar.gz` archive
restore the full working memory of the previous session. Read `AGENTS.md`
first — patch discipline and evidence discipline are load-bearing rules.

## 1. What this project is doing right now

Goal: run a genuine macOS Ventura 13.4 (22F82/22F66-era) userspace + real
WindowServer + Aqua desktop inside a chroot on a jailbroken M1 iPad
(iPad13,11, iPadOS 16.3 / 20D47, Dopamine rootless at `/var/jb`).

The iPad is the *target*; the Mac is only the *build/RE/control host*.
Nothing about the target changes when moving dev to x86 — all device work
goes over SSH exactly as before.

Milestone chain:

- DONE: `/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI` → `HI`, rc 0
- DONE: WindowServer runs (`-virtualonly -daemon`), publishes Mach services
- DONE: Dock survives, passes `_RegisterApplication`, receives a real LSASN
- DONE: private `macws-cfprefsd`, session+system `lsd`, `launchservicesd`
- DONE: display/input/interop daemons carry transport; one 2732x2048
  IOSurface frame reached the MacWSHost app ("首帧已就绪")
- OPEN: WindowServer never answers `final-composite` replay requests
  (`/var/mnt/rootfs/private/tmp/macws_final_composite.state` stays
  `state=missing`), so the app shows a black Metal canvas with no wait
  message. This is the current frontier — see §7.

## 2. Device access

- SSH: `ssh -p 2222 root@<iPad-IP>` — the last working address was the
  iPad personal-hotspot gateway `192.168.64.1`. If the hotspot renumbers,
  find the device on the LAN (port 2222, root).
- Auth: root password — **not committed to git** (AGENTS rule). It is the
  user's own device password; ask the user. Use `sshpass -p '<pw>'`.
- Host shell emits `failed to load module 'zsh/watch'` — harmless
  signature-invalid `/var/jb/usr/lib/zsh/5.9/zsh/watch.so`, ignore it.
- After **every iPad reboot / re-jailbreak**: trustcache is volatile.
  Re-run the bounded trust restore (`misc/macws_boot_trust.py` closure for
  the binaries you will exec) **before** expecting anything to spawn, and
  re-do the bind-mounts + devfs before chroot work. See §5.

## 3. Getting the dev host ready (x86 macOS)

1. `git clone` the fork (zenkernelsam/macPad), branch `main`.
2. Extract `macpad-gitignored-*.tar.gz` — it contains:
   - `analysis/` — all RE material: iOS 16.3 kernelcache + IDA databases
     (`kc_raw_16.3_T8112.bin*`, ~1.8 GB — the kernel RE state), extracted
     iOS 16.3 dyld cache (`ios163cache/`), Ventura framework leftovers
     (`device-leftovers-20261006/` incl. `dscq/usr/lib/system/libxpc.dylib`),
     dyld build work (`dyldwork/`), xnu source, nested reference repo
     `analysis/MacWSBootingGuide/` (has its own `.git`).
   - `theos/` — copy to `~/theos` (contains `iPhoneOS16.5.sdk`,
     `MacOSX15.5.sdk`; needed by every Makefile via `$(THEOS)`).
   - `packages/` — last built `.deb` payloads.
   - `tmp/` — dyld patch experiment artifacts (keep for reference).
   - `MANIFEST.txt` — file-by-file listing.
3. Merge into the clone: copy `analysis/` into the repo root (same relative
   path — scripts reference it), put `theos/` at `~/theos`.
4. Tools needed on the x86 host: Xcode CLT, `sshpass` (`brew install
   hudochenkov/sshpass/sshpass` or equivalent), `ldid` (Procursus build or
   `brew install ldid`), python3, `otool`, `vtool` (Xcode), optional IDA /
   ida-pro-mcp for RE, capstone via `pip`.
5. Theos on x86 builds arm64/arm64e iOS targets fine (clang cross-compile).
   Do NOT need Rosetta for the build itself.

## 4. Build and deploy pipeline (verified)

Aggregate build: `make` at repo root (Theos aggregate) builds
`launchservicesd`, `libmachook`, `macwsinputd`, `macwsdisplayd`,
`macwsinteropd`, proxies, `launchdchrootexec`, etc.

libmachook discipline — **thin slices, not fat** for insertion:

```
make -C libmachook            # produces fat dylib
# split slices:
lipo -thin arm64e <fat> -output libmachook.dylib.e
lipo -thin arm64  <fat> -output libmachook.dylib.a
# retag platform to macOS (REQUIRED — iOS-plat dylibs die under chroot):
python3 misc/set_macos_version.py <file>    # or vtool equivalent
# sign each slice:
python3 misc/cache_cdhash.py ...            # see postinst recipe
# deploy NEW INODES (never overwrite in place — trustcache keys on inode):
scp slice → /var/mnt/rootfs/usr/local/lib/libmachook{,_arm64}.dylib.new
mv .new → final path
# register every slice cdhash with the device trust mechanism
misc/macws_boot_trust.py <paths>            # prints BOOT-TRUST {"added":N}
```

`launchdchrootexec uid gid /path/to/root /path/to/exec args` is the launcher
contract: it chroots, drops uid/gid, selects `libmachook.dylib` (arm64e
targets) or `libmachook_arm64.dylib` (arm64 targets) for
`DYLD_INSERT_LIBRARIES`. LaunchDaemon plists live in
`/var/jb/usr/macOS/gui-launchd/`.

## 5. Cold-start sequence (after iPad reboot/re-jailbreak)

1. `misc/macws_boot_trust.py` — restore trust closure for every Mach-O you
   will exec (echo test, WS, Dock, daemons). Temperature `serious` gates
   the `trust` subcommand — the bounded python restore path works anyway.
2. Re-create bind mounts + devfs (see `misc/postinst.sh` /
   `layout/usr/macOS/bin/macos_gui.sh` mount section).
3. Baseline: `chroot /var/mnt/rootfs /bin/echo HI` → `HI`.
4. `launchctl load` the gui-launchd plists in dependency order:
   cfprefsd-daemon → launchservicesd → lsd → WindowServer →
   display/input/interop → Dock → SystemUIServer → Finder.
5. Stop everything cleanly: `bash /var/jb/usr/macOS/bin/macos_gui.sh stop`.

## 6. Findings bank (all evidence-backed this session)

XPC / LaunchServices:

- `bootstrap_look_up` on mapped `com.apple.macosbooter.*` names succeeds
  (`kr=0`). The failure was **activation**, not lookup.
- **`XPC_CONNECTION_MACH_SERVICE_PRIVILEGED` (0x2) is fatal on iOS
  launchd**: identical probe, flags=0 → handshake OK, flags=0x2 →
  `Connection invalid`. Fix in `libmachook/mac_hooks.m` (~line 15650):
  when a Ventura service name is remapped to `com.apple.macosbooter.*`,
  strip 0x2. Scoped to mapped names only — do not widen it.
- `xpc_add_bundle` interpose: any registration for
  `com.apple.MTLCompilerService` gets its path rewritten to the flat
  adapter bundle — launchd name-resolution races otherwise pick the stock
  iOS service (runtime-confirmed: pingflat landed pid 724 = /System
  version).
- launchservicesd must run the **stock** Ventura binary (fat arm64/arm64e)
  with project signing — `exec_to_dylib.py` produced an arm64 loader +
  arm64e payload pair that can never dlopen. The 13.4-validated flow is:
  stock exec + `launchservicesd.dylib` beside it, NOT a thin-loader swap.
- Diagnostics: `MACWS_XPC_NAME_TRACE`, `MACWS_LSXPC_TRACE` (dumps full
  request/reply dicts via interposed `xpc_connection_send_message_with_
  reply_sync`), `MACWS_BOOTSTRAP_PROBE`, `MACWS_CONN_INSPECT`,
  `MACWS_AGX_CRASH_DIAG` (SIGABRT handler with dladdr-symbolized stack).

Metal/compiler chain (working, don't regress):

- `Target OS is incompatible` root cause: bitcode spec record field +0x24
  must be `7` (LLVM `Triple::IOS`); macOS serialization writes `11`.
  Fix = offline `metal2metal.py` retarget of QC/SkyLight/MPSImage to
  `air64-apple-ios19.0.0-macabi` companion libraries + routing manifest;
  libmachook forwards `fixed_frag_lph_cpf` etc. at specialization
  boundary. **Requires on-device llvm-16** (Procursus) for translate —
  local llvm@22 output crashes the service's `fatalDiagnosticHandler`.
- Re-run `ensure_quartzcore_compat.sh` with llvm-16 if those libs change.

Signing/trust:

- Every Mach-O needs: project entitlement sign `ldid -S<entitlements> -M`
  (merges native ents) **twice** + all-slice cdhash registration.
  Stock-Apple-signed binaries die `OS_REASON_EXEC` under CT policy 0x8.
- Trustcache is volatile; new inodes need re-registration. SIGKILL=137 →
  exec denied; SIGTRAP/133-134 → dyld/jit layer.

Filesystem preconditions:

- `$ROOTFS/var/folders/zz/.../0/` dirs must exist for lsd csstore
  (`systemContentDatabaseStoreFileURLWithUID` crashes otherwise).
- `/var/mnt/rootfs/private/var/.TemporaryItems` 01311,
  `folders.0` 0700, `folders.0/TemporaryItems` 0700 for cfprefsd.
- `macws-cfprefsd` = private signed copy at
  `/usr/local/libexec/macws-cfprefsd`, launched via
  `com.macwsguide.cfprefsd-daemon.plist`.

## 7. Current frontier — read this first

Last device state before handover (2026-10-09 ~01:20 HKT, then clean
`macos_gui.sh stop`):

- Dock pid 23598 alive 14+ min, real ASN `C96172A0-…`, input socket
  `/var/mnt/rootfs/private/tmp/macws_app_input.23598.sock` registered →
  `system-input` should report `YES/<dockPID>`.
- displayd log (`/var/jb/var/mobile/macwsdisplayd.err`): one frame arrived
  (`frame-first ... surface=2732x2048`), then client unsubscribed; all
  `final-composite-replay-request attempt=N ... service=14623
  send=268435460` go unanswered → `macws_final_composite.state` =
  `state=missing`.
- Next move: find what `service=14623` (mach port) is and why WS never
  replies — check whether WS's compositor stalled on the earlier
  `EXC_GUARD reason_code 0x9` (LIBXPC guard) signature, and whether the
  composite producer ever initialized. Also still-unresolved: terminal
  AppKit launch aborted signal 6 before publishing a window (may be stale
  hostd `last_error` — verify before treating as current).
- `fullscreen-target-candidates catalog=[] layers=[]` — no app windows in
  the WS catalog yet.

## 8. Hazards

- Iterating `kickstart` on a doomed WindowServer **freezes the physical
  display** (WS dies holding the framebuffer; looks like a full hang — it
  was NOT a kernel panic; ResetCounter showed `btn_rst,btn_seq_reset`).
  Do bounded single WS runs and check display-stack health between runs.
- Never mass-`kill -9` system-domain services mid-experiment.
- Process uptime ≠ stability (AGENTS.md). Witnesses: completed XPC
  round-trips, ASN issuance, frame counters, endpoint registration.
- Do not commit device password, SSH keys, NAS credentials, or user
  hostnames — use `MACWS_DEVICE`-style placeholders (AGENTS.md).
- `.theos/` build-session symlinks are host-absolute — regenerate on x86
  with a fresh `make`; do not copy them.

## 9. Key file index

Repo: `libmachook/mac_hooks.m` (all interposes + diagnostics),
`layout/usr/macOS/bin/macos_gui.sh` (canonical start/stop),
`misc/postinst.sh` (signing + rootfs prep recipes — read it),
`misc/macws_boot_trust.py`, `misc/extract_dyld_cache.py`,
`misc/metal2metal.py`, `misc/lsxpc_probe.m` (XPC flags probe),
`macwshostd/main.m` (status keys incl. system-input =
dock-pid + `IsAppInputSocket`), `macwsinputd/main.c` (socket layout),
`macwsdisplayd` (composite/replay protocol), `MacWSHost/main.m`
(~line 6009, readiness gating).

Device: `/var/mnt/rootfs` (Ventura rootfs), `/var/jb/usr/macOS/gui-launchd`
(plists), `/var/jb/var/mobile/dock.log` + `WindowServer.err` +
`macwsdisplayd.err` (live evidence), `/var/mobile/Library/Logs/
MacWSHost.log` (app-side status).

Analysis dir (from the archive): `analysis/ios163cache` (extracted iOS
cache), `analysis/device-leftovers-20261006/var-mobile/dscq` (Ventura
dylibs for disasm — incl. libxpc, HIServices, LaunchServices),
`analysis/kc_raw_16.3_T8112.bin.i64` (IDA kernel DB),
`analysis/MacWSBootingGuide` (reference repo clone).
