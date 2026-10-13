# HANDOVER: macOS 15.6.1 Hello-World on iPadOS 16.3 — post-cache SIGSEGV hunt

**Date**: 2026-09-27 (evening) · **Repo**: `/Users/ciscohe/Desktop/macPad`
**Audience**: next agent — everything needed to reproduce and continue,
no prior context required. Live state file: `docs/porting/dyld-15.6.1-state.md`.

---

## 0. The task in one paragraph

Run a macOS 15.6.1 binary (`/bin/echo HELLO`) inside the chroot at
`/var/mnt/rootfs` on a Dopamine-jailbroken M1 iPad Pro (iPadOS 16.3,
kernel xnu-8792.82.2). The chroot's `/usr/lib/dyld` is a **patched macOS
15.6.1 arm64e dyld**. Everything up to "dyld finishes library loading"
works. The remaining blocker is a **SIGSEGV(139) or infinite burn loop
somewhere after library loading and before the app-entry call at dyld
`0x6b94` (`BLRAAZ X8`)**. Getting the crash PC (or proving which phase
burns) is the next milestone.

---

## 1. What is proven (do NOT re-derive)

| # | Fact | Evidence |
|---|------|----------|
| 1 | A chrooted process gets the **iOS shared region** attached at exec. `__shared_region_check_np` (syscall **294**) returns base **`0x1A4AE8000`** (same as native iOS procs). | freestanding check_np probes, native vs chroot |
| 2 | macOS dyld's `reuseExistingCache` **adopts the iOS cache** (magic `dyld_v1  arm64e` matches) and **loads ~90 iOS dylibs successfully** (libSystem, libnetwork, Security… + jb forkfix/libinjector). | `DYLD_PRINT_LIBRARIES=1` output, `/tmp/priv.log` |
| 3 | **The original project never patches the shared cache.** Upstream `MacWSBootingGuide` README lists only the GradedArchs arm64e→arm64 dyld patch ⇒ upstream design = adopt iOS cache + disk-load macOS-only libs. The 536/keeper/cachereg line is only needed IF macOS-cache contents turn out to be required. | upstream README |
| 4 | `__shared_region_map_and_slide_2_np` (syscall **536**) returns **EINVAL(22)** in the current environment; it DID return 0 historically only with `cachereg`(F_ADDFILESIGS)+slide-mask+`filescount1`, on an empty region. Irrelevant on the adopt-iOS-cache path. | errno probes (e5/m4 builds) |
| 5 | Process dies **after library loading, before `0x6b94 BLRAAZ X8`**: `entrymark` probe (writes `'E\n'+x8` to fd2 right before calling app entry) **never fires**. ⇒ crash/burn is inside dyld post-cache code or lib initializers, NOT in app code. | `dyld_emark.bin` runs |
| 6 | Behaviour is **non-deterministic** across identical runs: fast SIGSEGV(139) vs ≥45 s burn loop vs occasional 137. Always test ≥3× before concluding. | repeated runs |
| 7 | Chroot crashes produce **no `.ips`**; `run_nocskill MACWS_EXC=1` gets the **parent SIGKILLed by AMFI** (task_set_exception_ports gate); `spindump` gets SIGKILLed; `sample` outputs nothing. The reliable PC source so far: an **in-process SIGSEGV handler** (segvcap cave) or **dyld markers**. | runtime |
| 8 | dyld's own library search paths in chroot work; disk `libSystem.B.dylib` (34 KB dsc shim) loads after trustcache add but lacks `_err` → disk-fallback is a dead end; adopted-cache path is the way. | earlier fatal logs |
| 9 | `deployed /var/mnt/rootfs/usr/lib/dyld` is the binary under test; **`rm` then `cp` (never overwrite)** or the vnode CS cache goes stale → silent 137. | ops notes |

## 2. Exact repro

```bash
# device: sshpass -p cisco ssh -p 2222 root@192.168.64.1   (or USBmux proxy)
R=/var/mnt/rootfs
# pure chroot (no libmachook injection) — preferred for dyld debugging:
/var/mobile/run_nocskill /var/jb/usr/bin/env -i PATH=/usr/bin:/bin \
    /var/jb/usr/bin/chroot $R /bin/echo HELLO
# launchdchrootexec path (injects DYLD_INSERT_LIBRARIES=/usr/local/lib/libmachook_arm64.dylib):
/var/jb/usr/macOS/bin/launchdchrootexec 0 0 $R /bin/echo HELLO
# watch for: 'E' marker bytes, SIGNALED N, burns (ps %CPU)
```

Deploy a built dyld (each build must be re-signed + trustcached):

```bash
scp -P2222 analysis/dyldwork/<out>.bin root@DEV:/var/mobile/
LD=/var/jb/usr/bin/ldid; JB=/var/jb/basebin/jbctl
$LD -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist -M /var/mobile/<out>.bin
H=$($LD -arch arm64e -h /var/mobile/<out>.bin|grep CDHash=|cut -c8-)
$JB trustcache add $H        # verify with `trustcache info | grep -i $H`
rm -f $R/usr/lib/dyld; cp /var/mobile/<out>.bin $R/usr/lib/dyld; chmod 755 $R/usr/lib/dyld
```

## 3. Toolchain inventory (all local, all working)

- **`analysis/dyldwork/build_dyld.py`** — `python3 build_dyld.py <out.bin> [patchkeys…]`.
  Baseline = `analysis/dyld_15.6.1_arm64e_thin` (1240752 B). ⚠ The file is
  **co-edited by the user in their IDE — edits can be reverted**: re-read
  before building; verify the produced binary's bytes after build.
  - always-needed key: `crossarch` (@0x76270 svc→`mov x0,xzr`) else SIGSYS 140.
  - new this session: `entrymark`/`entrycave` (@0x6b94→`b 0x970`; cave writes
    `'E\n'+x8(8B)+'\n'` to fd2 then replays `BLRAAZ X8`).
  - diagnostic-only keys (do NOT ship): `hasexisting`, `prereuse`, `filescount1`,
    `dynoff`, `accessor`, slide-mask caves — they force the 536-map path which
    burns/crashes; not needed on the adopt-iOS-cache path.
- **`analysis/dyldwork/dyld_es.bin`** — built this session:
  `crossarch + entrymark/entrycave + segvcap hook/cave`.
  **Currently deployed** at `/var/mnt/rootfs/usr/lib/dyld`, SHA256-signed,
  CDHash `19bff783…` in trustcache → repro commands work as-is.
- **`analysis/dyldwork/dyld_segvcap.bin`** — existing dyld variant with an
  in-process SIGSEGV/SIGBUS handler: hook at `start()`+0x30 (`0x540c` →
  `b 0x9b578`), cave+handler at file offset **`0x9b578`** (free __TEXT tail
  padding, ~0xa88 B). Handler dumps siginfo(0x20)+ucontext(0x140) to fd2 →
  decode `pc`/`far` offline. Source: `analysis/dyldwork/segvcap_cave.s`.
  ⚠ This binary's other patches make it take the 536-map path → burns.
  **Next build should be `crossarch + entrymark + entrycave + segvhk +
  segvcave` only** (transplant recipe below).
- **`misc/run_nocskill.c`** — spawn suspended → KRW-clear CS_HARD|CS_KILL →
  resume → watchdog prints `child SIGNALED n`. Compiled binary on device at
  `/var/mobile/run_nocskill`.
- **IDA instances**: `ida-pro-mcp-Instance1` = dyld thin IDB
  (`analysis/dyld_15.6.1_arm64e_thin.i64`); `Instance2` = kernelcache
  (`analysis/kc_raw_16.3_T8112.bin`, base 0xfffffe0007004000). Use `py_eval`.
- **Device tools**: `jbctl trustcache info/add`, `ldid`, `/var/jb/usr/bin/chroot`,
  `keeper_test.sh`/`keeper_start.sh`/`next_boot.sh`/`com.macwsguide.keeper.plist`
  under `/var/mobile/` (keeper design = long-lived first-536-submitter —
  keep for later, not needed for adopt-iOS path).

## 4. dyld 15.6.1 hard-coded addresses (thin-slice offsets, RE-verified)

| Site | Meaning |
|---|---|
| `0x53dc` | `dyld4::start` function start; `0x540c` = early insn (segvcap hook site) |
| `0x6b94` | `BLRAAZ X8` — **call into app entry** (never reached yet) |
| `0x6a9c`, `0x6b48` | `BL start::$_0::operator()` — final lambdas before entry |
| `0x6b18/0x6b54` | `BL lsl::MemoryManager::lockGuard` + `writeProtect`/`unlock` @0x6b2c/0x6b40/0x6b70/0x6b78 — last memory-protect phase before entry |
| `0x34240` | `loadDyldCache`; `0x34298 BL reuseExistingCache`; `0x342d8 B mapSplitCacheSystemWide` |
| `0x351a8` | `reuseExistingCache` (check_np→strcmp magic→fill loadInfo→ret 1) |
| `0x352bc` | `mapSplitCacheSystemWide`; `0x35380` body start; `0x35660-0x35698` dynregion-append+536-call; `0x356d8 BL reuseExistingCache` post-map |
| `0x76dcc` | `__shared_region_check_np` stub (syscall 294); `0x76df8` 536 stub (x16=0x218) |
| `0xa9b10` | `_errno` global (cerror_nocancel @0x2d64 writes it) |
| `0x970`–`0xff8` | large free cave zone (load-commands end at 0x970) |
| `0x38d08`–`0x38d3b` | 56 B dead NOP cave |
| `0x9b578`+0xa88 | free __TEXT tail padding (segvcap cave location) |
| `0x2fe34` | `SyscallDelegate::getDyldCache`; caller frame seen in dumps = 0x2fe94 |

## 5. Next steps (ranked)

1. **Get the crash/burn PC.** ⏩ **`dyld_es.bin` is ALREADY built and
   DEPLOYED** at `/var/mnt/rootfs/usr/lib/dyld` (= `crossarch + entrymark +
   entrycave + segvcap-hook(0x540c→b 0x9b578) + segvcap cave@0x9b578`),
   SHA256-signed, CDHash `19bff783…` already in trustcache. Just run the
   §2 repro a few times:
   - SEGV run → handler dumps siginfo(0x20B)+ucontext(0x140B) on fd2.
     Decode: `far = siginfo+0x18`; in ucontext, `uc_mcontext` ptr at
     `+0x30` (user VA; mcontext usually inline in the same stack region)
     → `pc = *(mctx+0x100)`, `lr = *(mctx+0xf0)`, `sp = *(mctx+0xf8)`
     (mctx+0 = exception_state{far,esr,exception}, +0x18 = ss.x[0..]).
   - burn run (alive ≥2 s, 93 % CPU, silent) → no dump; go to step 2.
   To rebuild identical: `python3 build_dyld.py dyld_es2.bin crossarch
   entrymark entrycave`, then transplant:
   `d[0x540c:0x5410]=struct.pack('<I',0x1402585b)` and
   `d[0x9b578:0x9b778]=segvcap[0x9b578:0x9b778]` (0x200 B covers
   cave+handler; whole 0xa88 free zone is safe to copy).
2. **Phase markers between lib-load and entry.** Sites: `0x6a9c` (first tail
   lambda `start::$_0`), `0x6b18` (`lsl::MemoryManager::lockGuard`),
   `0x6b48` (second lambda). Each marker = replace the `BL` with `b <cave>`;
   cave writes one letter to fd2 then **executes the original BL itself**
   and returns to site+4 (LR/regs must survive — save x30!). Letters reveal
   the last reached phase (initializers? memory-protect? notify?).
3. If crash is inside **initializers**: suspect inserted iOS dylibs
   (forkfix/libinjector arrive via inherited `DYLD_INSERT_LIBRARIES` — test
   with `env -i` first; then with/without libmachook via launchdchrootexec)
   and **iOS libSystem initializers** running under a macOS-typed process.
4. If crash is inside **dyld memory-manager/ephemeral finalization**
   (`lsl::*`): compare against 13.4 flow; possibly the adopted iOS cache's
   `dynamicRegion`/`dyldData` layout differs → dyld writes through a
   relocated pointer.
5. **Do not chase 536/keeper for Hello-World.** Adopt-iOS-cache already
   delivers libs. Revisit only if macOS-only dylibs prove required.

## 6. Known traps (already burned sessions)

- `cp` over `/usr/lib/dyld` without `rm` → stale vnode CS → silent 137.
- `jbctl trustcache add` silently fails — always `trustcache info|grep -i`.
- `mov x1,sp` / any SP-sourced insn on misaligned SP → SIGILL (signature of
  corrupted-SP re-entry, seen at `0xe1030091`).
- Cave-at-0x970 writes must not store to image pages (text is RO) — use SP
  (valid on linear paths) or fd-write of constants.
- `ldid -S` rewrites fat slices/reorders; patch offsets are thin-slice only.
- `build_dyld.py` is co-edited — verify produced bytes each build.
- `spindump`/`sample`/MACWS_EXC all die on this device — use in-process
  handler or markers.

## 7. Success criterion

`/bin/echo HELLO` (or `/usr/bin/true` rc=0) inside chroot, repeatable.
Then document in `dyld-15.6.1-state.md` (Milestone rule, AGENTS.md) and
proceed to rootfs command coverage.
