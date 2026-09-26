# macPad toolkit & macOS-version porting playbook

For future sessions/AIs: what the project's built-in tools are, how they fit
together, and the proven procedure for bringing a new macOS rootfs up on the
jailbroken iPad. Read `dyld-15.6.1-state.md` first for live state.

## The architecture in one paragraph

`launchdchrootexec` (runs as root on iOS) chroots a process into a mounted
macOS rootfs (`/var/mnt/rootfs`) and injects `libmachook.dylib` via
`DYLD_INSERT_LIBRARIES`. The injected library interposes ObjC/IOKit/Core*
calls to adapt macOS binaries to the iOS kernel (arm64e, iPadOS 16.3).
iOS-side helper daemons (`macws*`) provide services the chroot can't reach
(IOSurface allocation, display, audio, input). Display goes either through
**coexist** mode (VNC streaming, iOS keeps the panel) or **exclusive** mode
(WindowServer owns the panel via IOMFB patches).

## Tool inventory

### Launch & injection

| Path | What it does |
|---|---|
| `launchdchrootexec/main.m` | The chroot launcher. `launchdchrootexec <uid> <gid> <rootfs> <target> [args]`. Records canonical host root into `MACWS_CHROOT_HOST_ROOT`, chroots+chdirs, drops uid/gid, picks ONE arm64/arm64e-appropriate `libmachook`, sets `DYLD_INSERT_LIBRARIES`, preserves HOME/TMPDIR. `MACWS_SUSPEND_AT_EXEC=1` / `MACWS_SUSPEND_TARGET=1` pause for lldb. argv items must be separate (no string splitting). |
| `libmachook/` | The interposition dylib injected into every chroot process. `mac_hooks.m` is the core (ObjC swizzles, IOKit shims, IOConnectCallMethod translation, SkyLight/QuartzCore fixes). `Metal_hooks.x`, `QuartzCore_hooks.x`, `exec_hooks.c`, `AppInputBridge.m`, `os_variant_hooks.x` per-subsystem. |
| `misc/chroot_then_exec.c`, `misc/chroot_isolation_test.c` | Minimal chroot+exec probes — verify chroot/vnode/env behavior without the full launcher. |

### Trust / signing / Mach-O surgery

| Path | What it does |
|---|---|
| `misc/loadtc` | Loads a trustcache file (plist of cdhashes) into the jailbreak trustcache — batch alternative to `jbctl trustcache add`. |
| `misc/vtool_and_sign.sh` | vtool fixups + `ldid -S` + trustcache in one shot for a binary. |
| `autosignd/` | Daemon that auto-signs newly-dropped binaries & registers cdhashes. |
| `misc/sign_installed.sh` | Re-signs everything under an installed tree. |
| `misc/arm64ify_macho.py`, `misc/set_to_arm64.py`, `misc/add_macho_load_dylib.py`, `misc/exec_to_dylib.py` | Mach-O surgery: retag slices, inject LC_LOAD_DYLIB, exe↔dylib conversion (e.g. running WindowServer as a dylib host). |

### dyld / shared-cache

| Path | What it does |
|---|---|
| `misc/sprobe.c` (+`misc/sprobe`) | **The shared-region syscall probe.** Freestanding static arm64e Mach-O (`-nostdlib -static -Wl,-e,_start`), raw `svc` only — runs WITHOUT dyld. Calls `shared_region_check_np`(294) and `shared_region_map_and_slide_2_np`(536) directly, prints kern_return errno for each stage. This is how you diagnose `dyld cache '(null)' not loaded` — no trampolines needed. |
| `misc/extract_dyld_cache.py` | Parses DSC header/mappings/images — extract a dylib from the cache or dump the mapping table. |
| `misc/disasm_remote_dyld_range.sh` | Byte-dump of on-device dyld ranges (for verifying deployed patches). |
| `misc/dlopen_probe.c`, `misc/csflags_dump.c` | dlopen behavior + csops code-sign flag dumps inside chroot. |

### Reverse-engineering

| Path | What it does |
|---|---|
| `misc/lldb_*.lldb` / `*.py` / `*.sh` | Scripted lldb: attach to suspended chroot process (`MACWS_SUSPEND_AT_EXEC=1`), set breakpoints by image-offset, dump registers/memory/fields. `lldb_trace_*.lldb` = ready-made traces for specific AGX/SkyLight functions. `ios_lldb_tmux.sh`, `lldb_remote.sh` = attach helpers. `lldb_mcp_setup.sh` = lldb↔MCP bridge. |
| `misc/mcp_stdio_tcp_bridge.py` | TCP bridge for MCP servers over the SSH pipe. |
| `analysis/` | Extracted thin slices for IDA (`dyld_15.6.1_arm64e_thin`, etc). |

### Display / input / verification

| Path | What it does |
|---|---|
| `misc/capture_vnc.py`, `misc/vnc_*.py`, `misc/capture_ipados.sh` | VNC framebuffer capture + input injection — the "did we render pixels" witness. |
| `misc/fbtest.py`, `misc/dispinfo.py`, `misc/cg*.py` | CG/framebuffer probes. |
| `misc/host_input_*.py`, `misc/host_key_probe.py`, `misc/host_gesture_probe.py` | iOS-side input → chroot delivery. |
| `misc/macws_frame_to_png.py`, `misc/macws_ipados_capture.m` | Frame→PNG capture. |
| `misc/skylight_geometry_probe.m`, `misc/macws_window_*.m/py` | Window/geometry probes. |

### AGX / Metal / GPU

| Path | What it does |
|---|---|
| `misc/agx_iogpu_probe.c`, `misc/agxprobe.m`, `misc/iogpu_symbol_probe.c` | iOS-side KRW: open AGXAccelerator UC, walk kernel objects, measure IOGPU fields — answers "what does the kernel check" without IDA kernel work. |
| `misc/ios_agx_*.m`, `misc/macws_agx_translate_replay.c` | AGX encoder/translate/replay probes. |
| `misc/iosurface_*`, `misc/TestMetalIOSurface` | IOSurface probes. |
| `MTLCompilerBypassOSCheck`, `MTLSimDriverHost`, `mtl_keepalive`, `misc/metal2metal*.py` | Metal driver plumbing for the MTLSim path. |
| `macwsallocd`, `macwsdisplayd`, `macwsinputd`, `macwshostd`, `macwsaudiooutd`, `macwsinteropd`, `macwskeychaind`, `macwslocationd`, `macwsthermal` | iOS-native helper daemons (launchd jobs `misc/com.macwsguide.*.plist`). |

### Packaging / deploy

| Path | What it does |
|---|---|
| `layout/` | .deb payload tree (`layout/DEBIAN/postinst`, `layout/usr/macOS/bin/*`). |
| `misc/postinst.sh`, `misc/install_rootfs_15.sh`, `misc/ipad_fix_deps.sh` | Rootfs install/fixup. |
| `docs/porting/rootfs-15.6.1-install.md` | How the 15.6.1 rootfs was built/mounted. |
| `build-rootfs-15.6.1.sh` (~/Desktop) | Rootfs build script. |
| `misc/cleanup_all.sh` | One-click stop of the whole chroot stack (use when crash-looping). |
| `misc/run_bash.sh`, `misc/run_steam_live.sh`, app `test_*`/`macws_*_probe*` | Per-app smoke tests. |

## Porting a new macOS version to the iPad — the proven procedure

### Phase 0: rootfs
1. Build/acquire the macOS rootfs (IPSW→OTA→dmg; see
   `rootfs-15.6.1-install.md` + `build-rootfs-15.6.1.sh`).
2. Mount/bind it at `/var/mnt/rootfs` (must survive across the task's
   lifetime — check `mount` output).

### Phase 1: dyld shared cache (the gate for everything)

**Check feasibility BEFORE any patching:**

1. Pull the main DSC header, read `sharedRegionStart@0xe0` and
   `sharedRegionSize@0xe8` (via `extract_dyld_cache.py` or IDA).
2. Compare against the iOS kernel's region constants — on iPadOS 16.3
   (xnu-8792): `SHARED_REGION_BASE_ARM64=0x180000000`,
   `SHARED_REGION_SIZE_ARM64=0x100000000` (4 GB → ends 0x280000000).
   These are compile-time kernel constants — NOT patchable.
3. If cache extent > region size → the systemwide `map_and_slide_2_np`
   path **cannot** take the full cache. Options:
   - Drop subcaches: patch dyld's `files_count` (thin 0x3538c → `MOV
     W28,#1`) — only works if everything needed is in the main file
     (check `extract_dyld_cache.py` for libSystem's image address;
     on 15.6.1 it's `0x18e6cf000` = in-bounds).
   - Relocate the trailing `fd=-1` DynamicRegion mapping to an in-bounds
     hole (patch the computation in `preflightCacheFile` AND the
     `dynamicRegion()` accessor to the SAME offset — accessor returning
     NULL → later `halt`).
   - Any subcache mapping that crosses the boundary must be dropped or
     relocated — but images baked at fixed VAs can't be moved.
4. If even the main cache doesn't fit → the systemwide path is dead;
   `mapSplitCachePrivate` (plain mmaps) is a dead end on iOS because exec
   mappings of the unsigned DSC get CS-killed. Then the version is likely
   not viable without a kernel-patchable shared region.

**Then instrument:**
- Compile `misc/sprobe.c` (`clang -target arm64e-apple-macosx14 -nostdlib
  -static -fno-stack-protector -Wl,-e,_start`), sign+trustcache, drop into
  rootfs, run via launchdchrootexec. It prints the real errno for each
  stage — separates "region occupied/ENOMEM" from "EPERM root-dir" from
  "EINVAL signature".
- Patch dyld per the recipe table in `dyld-15.6.1-state.md`.

### Phase 2: userspace bring-up
Order of smoke tests: `/usr/bin/true` → `/bin/echo` → a static probe →
`/bin/ls` → then daemons. Each new binary exercises more of the cache
(libdyld, libsystem_kernel, objc runtime come from the shared cache).

`DYLD_PRINT_*` env reaches the chroot dyld — use `DYLD_PRINT_WARNINGS=1`,
`DYLD_PRINT_LIBRARIES=1`, `DYLD_PRINT_SEGMENTS=1` to see which cache/libs
are being used. Distinguish iOS-side (launcher) prints from macOS-side by
the `[launchdchrootexec] target=` banner — everything after is the child.

### Phase 3: WindowServer (display)

See `re-analysis-15.6.1.md` + `PORT-TO-MACOS15-HANDOVER.md` + the
hit-rate table — the 13.4 patch points and their 15.6.1 offsets. Display
routes: coexist (VNC) vs exclusive (IOMFB panel takeover). The exclusive
route needs the IOMFB cluster + AGX path — see the AGENTS.md blocker table
(IOGPU `0xe00002c2` is the open kernel gate).

## Rules of thumb (burned-in lessons)

- **Never trust a silent rc.** `launchdchrootexec | tail/awk` eats `$?`.
- **`cp` over an existing file keeps the stale inode → kernel CS-kills.**
  Always `rm` then `cp` (or use a new path).
- **`jbctl trustcache add` with an empty var silently no-ops.** Always
  verify with `jbctl trustcache info | grep`.
- **`ldid -S` repacks the fat file** — arm64e slice delta changes; always
  re-read the fat header before writing patch bytes.
- **DSC file is CS-enforced** — editing it invalidates the signature for
  that inode forever. Patch dyld (a Mach-O you can adhoc-resign), never
  the cache.
- **iOS prebinds ITS cache into the chroot task's shared region** — macOS
  dyld's `reuseExistingCache` accepts it by magic alone. Forced-syscall
  runs must skip the pre-reuse (0x34298) but keep the post-syscall reuse
  (0x356d8) which populates `results->loadAddress`.
- **`check_np(NULL)` detaches the task's shared region** — the private
  path uses it; in an iOS-launched process it unmaps live libc → SIGBUS.
  Harmless inside a chroot'd macOS dyld.
- **brk-bisect exit codes**: 133=brk fired, 132=SIGILL, 137=SIGKILL
  (CS/trustcache), 139=SIGSEGV, 134=abort, 140=SIGSYS, 138=SIGBUS.
- **Every byte patch: derive the instruction in IDA (thin offset), write
  it on the device at `fat_delta + thin`, verify by reading back, re-sign,
  re-trustcache, fresh inode.**

### Kernel RE pipeline (iOS kernelcache → IDA)

The device carries its own kernelcache (IMG4) in preboot:

    /private/preboot/<hash>/System/Library/Caches/com.apple.kernelcaches/kernelcache

Extract on host:

    scp -P 2222 root@DEVICE:/private/preboot/*/System/Library/Caches/com.apple.kernelcaches/kernelcache /tmp/kc.img4
    python3 -m venv /tmp/kcvenv && /tmp/kcvenv/bin/pip install pyimg4
    /tmp/kcvenv/bin/python - <<'EOF'
    import pyimg4
    im=pyimg4.IMG4(open('/tmp/kc.img4','rb').read())
    p=im.im4p.payload; p.decompress()
    open('/tmp/kc_raw.bin','wb').write(p.data)   # Mach-O arm64e, ~76MB
    EOF

Then load `/tmp/kc_raw.bin` in IDA (full kernelcache incl. kexts — slow first
pass). `shared_region_map_and_slide_2_np` = BSD sysent[536];
`shared_region_check_np` = sysent[294]. The iOS-only errno values (e.g. the
EMSGSIZE=40 observed) live in this binary, not in open xnu.

### Exit-code probes inside dyld (works around "no custom binary exec")

- Custom Mach-O binaries get CS-killed on this kernel; only patch dyld
  itself or inject `libmachook.dylib` (ctor injection fails if dyld aborts
  early on libdyld).
- `exit(N)` sequences MUST sit at-or-after the syscall return point:
  exiting while the task's shared-region attach is in flight → SIGKILL
  (137), no crash report. Working probe slot: thin `0x35698` (first instr
  after `BL map_and_slide` returns).
- Probe pattern that works (errno→exitcode):
  `CMP W0,#-1; B.NE skip; ADRP X8,#0xa9; LDR W0,[X8,#0xb10]; MOV W16,#1;
   SVC #0x80; skip: MOV W0,#0x30; B <orig_flow>`
