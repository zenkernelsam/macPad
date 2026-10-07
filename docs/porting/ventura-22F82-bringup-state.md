# Ventura 13.4.1 / 22F82 bring-up — working state (2026-10-06)

> Ground-truth file for the macOS 13 Ventura branch. Keep it updated at every
> milestone before moving on. Evidence labels: `runtime-confirmed` /
> `RE-confirmed` / `source-confirmed` / `THEORY`.

## 0. Target and first milestone (source: user decision 2026-10-06)

- Guest userspace: **macOS 13.4.1 / 22F82** (UniversalMac restore IPSW).
  22F82 is the newest build inside the author-validated Ventura family
  (13.4/22F66 and 13.4.1/22F82 share one cache CDHash pair in
  `postinst.sh`/`macos_gui.sh`). macOS 13's true last release is 13.7.x
  but is NOT in the validated family — not the target.
- First milestone (unchanged):
  `/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI` printing `HI`
  through the real 22F82 dyld + real shared cache.
  A disk-shim `HI`, shim libSystem, process survival, installer rc=0, or
  trust-helper rc=0 do NOT count.
- Historical note (`CLI-MILESTONE-2026-09-28.md`): the earlier
  `echo HELLO`/`cat`/`sh` passes used a **disk libSystem shim**; syscall 536
  was failing with error 40 at that time. Those are shim witnesses, not
  cache witnesses.

## 1. Device baseline (runtime-confirmed, read-only, 2026-10-06)

- iPad13,11 / iPadOS 16.3 / 20D47, Dopamine rootless, 1.1 TiB free on
  /private/var.
- `/var/mnt/` holds only empty `r2/` — no rootfs, no `rootfs-13.4.bak`.
- `com.kdt.macosbooter 0.3.4` package intact: `/var/jb/usr/macOS`
  complete, all launch plists present, jbctl/python3/ldid/mount_bindfs/
  uicache/chroot/bash present.
- Running: `macwskeychaind`, `MacWSHost` app; `alloc`/`hostd` jobs loaded
  but not running. No chroot/WindowServer/15.6 runner remnants.
- Live trustcache: 188 entries, none of the 4 known macOS cache CDHashes
  admitted this boot.
- Residuals archived to `analysis/device-leftovers-20261006/` (local only,
  untracked): `dscq/` (217 M iOS DSC dylib extraction, re-derivable),
  `macws-runtime-stage/` (428 K), `__pycache__/cdhash.pyc`,
  `macwsallocd.{err,out}`, `/var/jb/var/mobile/MacWSBootingGuide/` (428 K
  partial repo). Inventory evidence:
  `docs/evidence/ventura-handover-device-inventory-20261006.md`.
- `/var/mobile/k*.py` kernel RE scripts are already absent.

## 2. Assets (source-confirmed)

### 22F82 IPSW — downloading

```text
URL      https://updates.cdn-apple.com/2023SpringFCS/fullrestores/042-01877/2F49A9FE-7033-41D0-9D0C-64EFCE6B4C22/UniversalMac_13.4.1_22F82_Restore.ipsw
sha256   5ac144d1661614806d765bc0466d719152e2594c2db3888f1ac02276f5638e98   (appledb)
size     ~12.5 GB (22F66 entry listed 12,739,995,153 B; verify after fetch)
local    VirtualMacOniPad/VirtualMac/build/downloads/UniversalMac_13.4.1_22F82_Restore.ipsw.part
method   curl -C - (resumable); rename .part -> .ipsw only after sha256 match
devices  includes MacBookAir10,1 / VirtualMac2,1 (appledb deviceMap)
```

### 22D68 assets — retired to Trash (recoverable)

User ordered 13.2.1 removed on 2026-10-06 after choosing 13.4.x:

```text
~/.Trash/UniversalMac_13.2.1_22D68_Restore.ipsw        (12.49 GB)
~/.Trash/22D68__MacOS-20261006/                        (extracted cache + .a2s + payload frameworks)
~/.Trash/22D68__MacBookAir10,1/                        (kernelcache only)
```

Caveat recorded: VirtualMac's shipping payload was built from 22D68;
rebuilding the VM inputs would need these back.

## 3. What the existing package already gives us (source-confirmed)

- **Trust contract already supports 22F82.**
  `postinst.sh:1066` and `macos_gui.sh restore_cold_boot_trust:1686` map
  `22F82|22F66|""` to the Ventura cache pair
  `b5da39409492ac85e5a8e8ab618fe77e2d7a2980` +
  `bbb765988e2677b98d47a549d612fa0d4af25f69`. No code change needed for
  trust admission — only runtime verification after install.
- **Cache path convention**: package expects cache at
  `$ROOTFS/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/
  dyld_shared_cache_arm64e{,.01}` — runtime-confirmed for Ventura on
  iPad13,11/iPadOS 16.4.1 (`layout/DEBIAN/postinst:47-62`,
  `normalize_restored_runtime_metadata`).
- **Loader closure**: `postinst.sh` admits
  `/var/mnt/rootfs/usr/lib/dyld` (the real dyld, expected as a file inside
  rootfs) plus chroot `CydiaSubstrate`.
- **`macos_gui.sh trust`** may pause on `thermal-state=serious`
  (FROZEN doc); plan around it for device runs.
- **`misc/cdhash_slices.py` / `misc/cdhash.py`** exist for computing cache
  CDHashes if the hardcoded pair needs verification.

## 4. Open questions before first exec (marked, not assumed)

| # | Question | Status |
|---|---|---|
| Q1 | Is the author's 13.4 dyld **stock** or **patched**, and with what? DEBIAN/postinst:84 says the chroot executes "patched macOS dyld/shared-cache images". `postinst.sh` does not patch dyld — so the patch must live inside the rootfs image itself. Fresh 22F82 rootfs will ship **stock** dyld. | OPEN — inspect zhuowei tool lineage + repo history; test stock first |
| Q2 | Old `syscall 536 → 40` failure: RE-confirmed producer is Sandbox `mpo_file_check_mmap` (`cred_sb_evaluate` of `file-map-executable`) firing on a vnode **lacking VSHAREDCACHE**. Was that failure specific to the 15.6 cache/layout, or will a legit 22F82 cryptex cache carry the flag and pass? | OPEN — likely non-issue for a proper cryptex-placed cache; verify at runtime |
| Q3 | The 15.6 PMAP-CS nested-owner bug (frozen branch root cause) — does it also bite a Ventura cache mapped through the **systemwide** 536 path? THEORY: no — 536 registers the cache AS the shared region rather than private-mapping inside the iOS SR submap; the author's 13.4 devices ran it. | THEORY — runtime verdict at first exec |
| Q4 | 22F82 IPSW OS volume contains no on-disk `libSystem.B.dylib` (macOS 11+ design; §19.6 for 22D68, same family). For `echo`, dyld resolves libSystem from the cache — no disk stubs needed. | THEORY — confirm with `DYLD_PRINT_LIBRARIES` |
| Q5 | `install_rootfs_15.sh` is 15.6-shaped (build checks `24G90`, arm64ify targets, swap logic using `rootfs-13.4.bak`). Needs a version-clean `install-rootfs-13.sh` — never reuse the 15.6 script as-is. | DONE — `misc/install_rootfs_13.sh` (22F82) + `misc/build-rootfs-13.4.1.sh` |
| Q6 | `usr/lib/systemhook.dylib` inside rootfs is trust-listed by `postinst.sh:1467` but **absent on this device** (`/var/jb/usr/lib/` has no `systemhook.dylib`; ElleKit uses `libhooker.dylib`/`libellekit.dylib`/`libinjector.dylib` instead). Old rootfs backups that used to carry it are deleted. | OPEN — candidate: harvest `libhooker.dylib` under a documented name, or source the original systemhook build; only needed for the chroot tweak-injection chain, NOT for the echo milestone |
| Q7 | Hardcoded "Ventura" pair `b5da3940…`/`bbb76598…` is **NOT** the stock 22F82 cache signature. `codesign -vvv -d` on the verified IPSW gives `7a3e85f1ddcb90e7d785bbfd6232fd058b4de317` / `2573536d64cbd47872f3d318bf0efc6273d7cf20`. The historical pair is now pinned to `22F66` only; `22F82|""` registers the stock pair in `postinst.sh`, `misc/postinst.sh`, `macos_gui.sh`, `DEBIAN/postinst`. | runtime/source-confirmed (codesign, commit 1656f07) |
| Q8 | `misc/cdhash.py` read CodeDirectory `hashType` at offset 34 (inside `codeLimit`; real offset is 37) → every sha256-typed CD fell into the sha1 fallback and printed hashes AMFI never matches (e.g. dyld `4e1b7d94` instead of `02a8a781`). Fixed + added `misc/cache_cdhash.py` (parses cache header csOff@0x28 → superblob slot 0 → hashType-aware digest). | source-confirmed, fixed |

## 4.2 Rootfs staging (source-confirmed 2026-10-06)

- `UniversalMac_13.4.1_22F82_Restore.ipsw` sha256 `5ac144d1…` matches appledb.
- OS volume (096-09648-077.dmg via `ipsw mount fs`): `ProductBuildVersion=22F82`,
  fat x86_64+arm64e `/bin/echo` + `/bin/bash`, 3-slice dyld, SkyLight
  `WindowServer` present, `/private/etc` empty (sealed volume).
- arm64e cryptex (096-09706-080.dmg via hdiutil): cache pair + `.map`,
  `usr/lib/system|swift`, `System/iOSSupport`, Frameworks — copied whole
  minus x86_64/aot caches (~2.5GB dead bytes on iOS).
- `misc/build-rootfs-13.4.1.sh` staged `~/Desktop/macos-13.4.1-rootfs`:
  System + Templates/Data skeleton (incl. stock `private/etc`) + cryptex +
  symlinks; cache ledger verified `7a3e85f1`/`2573536d` on staged bits.
- `/bin/echo` links only `/usr/lib/libSystem.B.dylib` → resolves via cache.
- Deployment in progress: tar stream → `/var/mnt/macos-13.4.1-rootfs.tar`
  on device, then `misc/install_rootfs_13.sh` (22F82 verify, arm64ify
  WindowServer/InstallerProgress/bash/sh/echo, swap, harvest, bindfs,
  postinst, run_bash smoke).

## 4.1 Injection sources on device (runtime-confirmed 2026-10-06)

```text
OK   /var/jb/usr/lib/TweakLoader.dylib
OK   /var/jb/Library/Frameworks/CydiaSubstrate.framework
OK   /var/jb/usr/lib/ellekit/{pspawn,MobileSafety,libinjector}.dylib
OK   /var/jb/usr/lib/{libsubstrate,libhooker,libellekit,TweakInject}.dylib
OK   /var/jb/usr/lib/TweakInject/{MacWSCatalystLaunch,MacWSWindowing,
      MTLCompilerBypassOSCheck,VZKeyboardPassthrough}.dylib (+ plists)
MISS /var/jb/usr/lib/systemhook.dylib
MISS /var/jb/usr/lib/CydiaSubstrate.framework
MISS /var/jb/var/mobile/theos
```

`launchdchrootexec` injects `DYLD_INSERT_LIBRARIES=libmachook` itself
(`main.m:61-72`), so the echo milestone does not depend on the chroot
tweak-injection chain. The `systemhook`/`TweakLoader`/`CydiaSubstrate`
closure is required later by `postinst.sh:1463-1467` and chroot-side tweaks.

## 5. Execution plan (current)

```text
A ✅ device/host inventory + residual archive   (evidence committed 6cba769)
B host staging (no device touch):
  B1 finish 22F82 download -> sha256 verify -> rename .ipsw
  B2 ipsw mount fs -> inventory OS vol; ipsw mount sys -> cryptex cache
  B3 build-rootfs-13.4.sh: OS vol {System,usr,bin,sbin} +
     Templates/Data skeleton + cryptex cache -> dyld dir +
     /System/Volumes/Data, /etc->private/etc, /tmp->private/tmp,
     /var->private/var symlinks + SystemVersion.plist check ==22F82
  B4 host-verify /bin/echo dep closure + dyld identity/UUID/CDHash pair
     vs postinst constants (must equal b5da3940…/bbb76598…)
C device install:
  C1 write misc/install-rootfs-13.sh (version-clean; fresh-inode rules;
     keep old rootfs backup semantics)
  C2 content-verified transfer (rsync --partial --append-verify)
  C3 postinst.sh + trust verify (jbctl trustcache info membership)
D bounded echo milestone, capture stdout/stderr/rc/DYLD_PRINT_LIBRARIES/
  crash reports; only then cat/sh/WindowServer.
```

## 6. Hard rules carried forward

- No kernel/PAC/PTE writes; no `fmt13_patch.py`; no 15.6 patch carry-over.
- No NOP/forced-branch/constant-return/zero-blob "fixes".
- `jbctl trustcache add` can no-op silently — always re-check `info`.
- `cp` over an existing signed file keeps stale inode → `rm` first / new
  inode (AMFI Invalid Page lesson).
- IDA Pro MCP for binary RE; verify `server_health` input_path/imagebase
  before citing addresses.
- Every commit → immediate `git push origin main`.
- Apple payloads (rootfs/IPSW/cache/archives) never enter git.

## 7. Milestone witness: `/bin/echo HI` prints HI (2026-10-06 ~22:02)

**Status: runtime-confirmed, reproducible (2/2 runs + reruns).**

```text
$ /var/jb/usr/macOS/bin/launchdchrootexec 0 0 /var/mnt/rootfs /bin/echo HI
chdir: No such file or directory
[launchdchrootexec] target=/bin/echo arch=arm64 \
    insert=/usr/local/lib/libmachook_arm64.dylib
HI            <-- real stdout from macOS 13.4.1 echo
rc=0
```

Authenticity basis: the exec chain is stock 22F82 `/usr/lib/dyld`
(arm64e slice) + the stock 22F82 split shared cache + cache-resident
`libSystem.B.dylib`/`libsystem_darwin.dylib` (crash `.ips` usedImages show
`source:"P"` = cache). No shim `libSystem`, no disk copy — echo's deps all
resolved from the cache (`P`). Output is a completed functional
transaction, not a keep-alive.

### The 137 wall: root cause chain (all runtime-confirmed)

Earlier `/bin/echo` runs died `RC=137` with **zero dyld output**. Two
independent BRK-at-entry probes (brk#0 written over `__dyld_start` file
off 0x4a40 = `mov x0,sp`, and over echo's `pacibsp` at arm64 entry) both
still returned 137 with no SIGTRAP and no `.ips` — **neither dyld's nor
echo's first instruction ever executed**; the kill is in kernel exec
admission / first-exec-page CS validation, not userspace.

Control run: `chroot /var/mnt/rootfs /var/jb/usr/bin/true` reaches iOS
dyld (prints diagnostics, rc=134) — exec inside the chroot is fine; the
kill is specific to the macOS-signed image admission.

**Fix (runtime-confirmed)**: re-sign both `bin/echo` and `usr/lib/dyld`
with `ldid -Hsha256 -S"$ENT"` (project entitlements plist, **without**
`-M`), then `jbctl trustcache add` each new CDHash. This matches the
15.6-era documented recipe (`dyld-15.6.1-state.md` fact ②: plain `-S`
/Apple-ent-merged signatures get killed at exec; `-Hsha256 -S<项目ent>`
admits). After re-signing:

1. macOS dyld ran, mapped the 22F82 cache, loaded images — crash `.ips`
   then showed a *userspace* `EXC_BREAKPOINT (brk 1)` inside
   `libsystem_darwin.dylib` `_check_internal_content.cold.1`, called via
   `os_variant_has_internal_diagnostics` from `libSystem_initializer`.
   libmachook's `DYLD_INTERPOSE` on `os_variant_has_internal_diagnostics`
   (`libmachook/os_variant_hooks.x`) bypasses it at bind time — so the
   milestone requires the `launchdchrootexec` insert path; plain
   `/var/jb/usr/bin/chroot` still SIGTRAPs there by design.
2. With `launchdchrootexec`, dyld then aborted loading
   `libmachook_arm64.dylib`: `@rpath/CydiaSubstrate.framework` resolved
   only to iOS-built copies — "mach-o file, but incompatible platform
   (have 'iOS', need 'macOS')". The author's rootfs shipped a macOS-built
   CydiaSubstrate we do not have.
3. Scaffold `misc/cydiasubstrate_macos.m` provides a macOS-platform
   `CydiaSubstrate.framework/CydiaSubstrate` (fat arm64+arm64e) with real
   `MSGetImageByName`/`MSFindSymbol` (dlsym + bounded export-trie walk +
   symtab fallback) and **no-op MSHookFunction/MSHookMessageEx stubs**
   (labeled diagnostic seam — GUI hook paths still need a real
   substrate). Result: `HI`, rc=0.

### Current known deltas / not yet done

- Plain `chroot` (no insert): rc=133 SIGTRAP at the os_variant check —
  expected; the interpose is the product fix. If the acceptance line must
  be the literal `chroot` binary, that path needs an equivalent
  interpose carrier, not a rootfs change.
- arm64e targets (`/usr/bin/true`, `/bin/cat`): still `rc=137` — arm64e
  exec admission is a separate signing surface (only arm64/ALL slices
  admitted third-party per 15.6 doctrine). Cat run printed nothing and
  launcher reported `arch=arm64e` → inserted `libmachook.dylib`.
- `chdir: No such file or directory` from the launcher (harmless;
  launcher chdirs into a path absent in the fresh rootfs — identify and
  fix cosmetically later).
- `MSHookFunction`/`MSHookMessageEx` are currently no-ops: exec_hooks /
  Metal / HID hook installation is NOT active. os_variant and other
  `DYLD_INTERPOSE` fixes ARE active (bind-time).
- Rootfs still lacks most on-disk system libraries (cache-resident only)
  — fine for the CLI milestone; GUI closure will need the remaining
  closure verification.
- The `id` binary earlier produced `id-*.ips` crash — unexamined.
- Device state deltas to re-verify on reboot: dyld/echo/CydiaSubstrate
  CDHashes must be re-added by the cold-boot trust path — extend
  `macos_gui.sh`/`postinst.sh` restore list if they aren't already.

### Files changed on device during this session (for reproducibility)

- `/var/mnt/rootfs/usr/lib/dyld`: restored to prior signed image, then
  re-signed `ldid -Hsha256 -S$ENT` (twice). Stock content otherwise.
- `/var/mnt/rootfs/bin/echo`: previous-session signature replaced by
  `ldid -Hsha256 -S$ENT` (twice). arm64 slice content stock.
- `/var/mnt/rootfs/System/Library/Frameworks/CydiaSubstrate.framework/
  CydiaSubstrate`: replaced harvested iOS build with host-built
  macOS scaffold (`misc/cydiasubstrate_macos.m`).
- Trustcache (dynamic, this boot): dyld slices
  `941b648c…/2045e3de…/92f445a6…` (i386/x86_64/arm64e), echo slices
  `8584ae5e…`(x86_64)/`294ebee8…`(arm64), cache pair
  `7a3e85f1…`/`2573536d…`, CydiaSubstrate scaffold
  `b6882124…`(arm64)/`68df5c7a…`(arm64e). Earlier `02a8a781…`/`3fa2adad…`
  hashes are stale — trust must always be re-derived from the final
  signed image, never copied forward.


---

## 2026-10-07 — cold-reboot verification + os_variant root cause (RE-complete)

### Reboot behaviour (runtime-confirmed)

- After device reboot + re-jailbreak, **all** trustcache CDHashes were
  still LIVE (dyld slices, echo arm64 `294ebee8…`, CydiaSubstrate
  `b6882124…`, 22F82 cache pair `7a3e85f1…`/`2573536d…`). Dopamine's
  `jbctl trustcache add` persists across reboot on this build — the
  earlier "dynamic = volatile" assumption is corrected. The
  `restore_cold_boot_trust` path (now including `bin/sh`, `bin/echo`,
  commit `9d66403`) remains valuable as verification/repair.
- `launchdchrootexec 0 0 /var/mnt/rootfs /bin/echo HI` → `HI`, rc=0 —
  survives reboot. (Launcher prints `chdir: No such file or directory`
  because the host cwd is absent inside the chroot; falls back to `/` —
  cosmetic only.)
- Bare `/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI` → rc=133,
  **unchanged** (earlier "rc=0" was a pipeline artefact: `$?` measured
  `head`, not chroot).
- New: `DYLD_INSERT_LIBRARIES=/usr/local/lib/libmachook_arm64.dylib
  /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI` → **HI, rc=0.**
  The literal chroot binary succeeds with env-level interpose — the
  launcher's only magic over bare chroot is supplying this env var
  (+ chdir/uid handling).

### os_variant trap — full root cause (RE-confirmed, libsystem_darwin 22F82)

`_os_variant_has_internal_diagnostics` (extracted image 0x…77c8):

```
once(token @dirty+0xa20) → flag byte @0x9fd ("diagnostics" override)
  !=0 → return 0            ; override honored
  ==0 → _check_internal_content(0x786c)
          status @0xa00: 2→0, 3→1, else → os_assert("os_variant had
          unexpected status") → brk #1     ← OUR TRAP
```

Once-init (0x9104): `sysctlbyname("kern.osvariant_status")` —

- **succeeds** → bit-parse: bit1 gates writing `status&3` into @0xa00,
  bit3→@0xa08, bit15→@0xa18, bit25→@0xa1c, bit7→@0xa04, bit9→@0xa0c,
  bit19→@0xa14, bit23→@0xa10, bits[48:52)→@0x9f8. Then
  `ldrb global-byte` — **byte==0 → `b epilogue`, SKIPS file fallback
  AND the `/var/db/os_variant_override` parser** (which sits at the end
  of the fallback path, reached only via the per-field loop).
- **fails** → per-field loop resolves each field by file/sysctl
  (`AppleInternalVariant.plist`, `hw.ephemeral_storage`,
  `csr_check(0x10)`, `AppleFactoryVariant.plist`, `BaseSystem`,
  `DarwinVariant.plist`, InternalDiagnostics plist), then override parse.

Measured on device (iPadOS 16.3 kernel):

- `kern.osvariant_status` = `0x7000000100000028` → sysctl succeeds,
  bit1=0 → **@0xa00 (internal_content) never resolved → stays 0**
  → `_check_internal_content` asserts. `[x22]` global byte is 0 on this
  path → file fallback + override never run.
- `kern.osvariant_status` is **read-only** (`sysctl -w` → EPERM).
- Legacy per-feature sysctls (`kern.osvariant_has_internal_content`,
  …diagnostics, …ui, allows_internal_security_policies, is_recovery,
  is_baseos) **do not exist** on iOS 16.3 — only the consolidated
  bitmask.
- `os_variant_override` file experiment: created
  `/var/mnt/rootfs/var/db/os_variant_override` =
  `content,diagnostics,ui,security` → bare chroot still rc=133, same
  `.ips` stack. Confirms the parse is unreachable on the
  sysctl-success/non-internal path. File left in place (inert here,
  correct on fallback paths).
- `os_variant_init_4launchd(<feature>)` (export @0x8c40) accepts
  "fvunlock/kcgen/diagnostics/migration/eacs" and calls the override
  parser — but only launchd invokes it; nothing in the chroot does.

**Conclusion:** on an iOS kernel the literal zero-env `chroot` cannot
pass `os_variant_has_internal_diagnostics` — no file, env, or sysctl
lever reaches the resolution path. Interposition is required; both
carriers now proven (`launchdchrootexec` and `DYLD_INSERT_LIBRARIES` +
literal chroot binary). This is the upstream-designed constraint, not a
rootfs defect.

### CLI breadth (runtime-confirmed, same evening)

- Generic admission recipe confirmed for both arches: any stock
  arm64/arm64e binary → `ldid -Hsha256 -S$ENT` ×2 +
  `macws_boot_trust.py --readd <file>` (registers the arm-family slice
  CDHashes; x86_64 slices are correctly skipped). `/usr/bin/true`
  went 137 → 133 → rc=0 with `libmachook.dylib` (arm64e slice) and
  via `launchdchrootexec`.
- `/bin/cat /tmp_test.txt` → real file content via launcher
  (arm64e path) AND via `DYLD_INSERT_LIBRARIES` + literal chroot.
- **`/bin/sh` is a dash-12 re-exec shim**: reads `/private/var/select/sh`
  (absent → default `/bin/bash`) and execs it. First `sh -c` attempts
  died 137 in the CHILD — not the sh image itself — because `bash`'s
  arm64 slice was signed but its CDHash had not been registered this
  boot. Lesson: **a 137 on a re-exec shim usually means the exec
  TARGET is untrusted, not the invoked path.**
- After signing+trusting bash/dash/zsh:
  `launchdchrootexec 0 0 /var/mnt/rootfs /bin/sh -c
  "/bin/cat /tmp_test.txt; /bin/date +%H:%M; /usr/bin/id -u;
   /bin/ls /bin/echo"` → all grandchildren (cat/date/id/ls) produce
  real output, rc=0. `DYLD_INSERT_LIBRARIES` propagates to children,
  so every exec'd grandchild gets the interpose bound automatically.
- arm64ify coverage is inconsistent in the installed rootfs:
  sh/echo/bash carry `sub=0` (arm64) slices; cat/true still `sub=2`
  (arm64e). Both admit under the recipe — arch only selects which
  libmachook slice must be inserted (arm64 → `_arm64.dylib`,
  arm64e → `.dylib`). install_rootfs_13.sh + cold-boot restore list
  now cover cat/dash/zsh/ls/pwd/date/ps/true/env/id.
- Caveat: `DYLD_PRINT_LIBRARIES`/`DYLD_PRINT_INITIALIZERS` inside the
  chroot produced a dyld `setUpLogging` SIGBUS on at least one run
  (log stream open path). Verbose dyld env debugging is not reliable
  inside the chroot; prefer `.ips` postmortems.

### Literal bare `chroot` — ACHIEVED (2026-10-07, runtime-confirmed)

```
/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI   →  HI, rc=0
/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/cat f      →  real file bytes
/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/date       →  real time
/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/ls /bin    →  38-entry listing
/var/jb/usr/bin/chroot /var/mnt/rootfs /usr/bin/id     →  uid=0(root) …
/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/sh -c …    →  nested output
                                                       (sh re-execs bash;
                                                        bash carries the
                                                        same mechanism)
```

Mechanism: `LC_LOAD_DYLIB` on `/usr/local/lib/libmachook_arm64.dylib`
(arm64 slices; `libmachook.dylib` for arm64e) added by
`misc/add_macho_load_dylib.py` — zero env, stock `chroot`, stock dyld,
interpose binds at load time before any initializer runs. This is a
declared, inspectable Mach-O dependency (same class as the project’s
documented `LC_LOAD_DYLIB` CydiaSubstrate on MacWSWindowing), not an env
or wrapper trick. Each binary still requires the signed+trusted recipe.

Rejected on the way: `LC_DYLD_ENVIRONMENT`
(`LC_ENVIRONMENT_DYLD_INSERT_LIBRARIES`, 0x27) carrying
`DYLD_INSERT_LIBRARIES=…` — added, signed, trusted, and **ignored** by
22F82 dyld (1066.8): bare chroot still SIGTRAP’d at os_variant. Kept as
`misc/add_macho_env_insert.py` with an EXPERIMENTAL docstring.

Caveat: binaries NOT carrying the load command and not run under an
insert env still hit the 133 trap (iOS kern.osvariant_status is
unchangeable). The mechanism must be applied per-binary at install
(`install_rootfs_13.sh` now does this for the CLI set).

### Full CLI sweep (2026-10-07, runtime-confirmed)

Audited every entry in `bin`, `sbin`, `usr/bin`, `usr/sbin` on the
deployed rootfs (manifest: `docs/evidence/cli-manifest-20261007.json`):

| Class | Count | State |
|---|---|---|
| Mach-O with working libmachook dep | 918 | literal `chroot` runs them |
| Mach-O, no header padding for the load command | 4 | `awk`, `csreq`, `scp`, `rpcinfo` — need env insert |
| Non-Mach-O (perl/.d/sh scripts etc.) | 298 | interpreter-dependent |

Verified under literal bare `chroot` this session (rc=0, real output):
`uname -a` (Darwin 22.3.0 / iPad13,11), `sw_vers` (macOS 13.4.1 / 22F82),
`hostname` `sysctl kern.hostname` `uptime` `uuidgen` `whoami` `id`,
`file` `stat` `plutil -lint` `xmllint` `diff` `du` `wc` `sort` `sed`
`grep` `find` `xargs` `tar`/`pax` `gzip` `zip`/`unzip` `sqlite3`
`openssl` (LibreSSL 3.3.6) `curl 7.88.1` `vim --version` (macOS arm64),
`cp`/`mv`/`rm`/`touch` mutations, `test` `ps` `vm_stat` `ioreg` `arch`
`open` (reached real LaunchServices, OSStatus -10661 as expected).
`python3`, `git`, `lldb`, `otool`, `nano` are stock Ventura
xcode-select stubs/TERM-dependent — genuine behaviour, not failure.
`df` fails on `getattrlist` (kernel lacks macOS-only attr; documented
kernel-API gap, not a signing or injection failure).

Load-command fix found during the sweep: dual-slice binaries
(`file`, `mpioutil`, arm64+arm64e) declared the arm64-only
`libmachook_arm64.dylib` name on their arm64e slice and could not
load it. Resolved by shipping the **fat** (arm64+arm64e) libmachook
under BOTH names — Makefile staging now copies the fat object to
`libmachook_arm64.dylib` instead of thinning, so either dep path
resolves for either slice. `install_rootfs_13.sh` second pass
therefore uses a single dep path with no per-slice detection.

Cold-boot restore: `macos_gui.sh` `restore_cold_boot_trust` now globs
the bounded `$ROOTFS/bin` + `$ROOTFS/sbin` sets and the curated
`usr/bin` list, so the restore stays synchronized with whatever the
installer signed (unsigned files contribute no hashes).

### Plugin-closure trust (2026-10-07, runtime-confirmed)

dlopen'd plugins need CS admission exactly like executables; the exec
sweep did not cover them. Registered **existing Apple signatures** for
every Mach-O under `System/Library`, `usr/lib`, `usr/libexec` — no
re-signing, no file modification:

```
macws_boot_trust.py $ROOTFS/System/Library $ROOTFS/usr/lib $ROOTFS/usr/libexec
→ files=214087 images=3725 hashes=3566 added=3492 (63 s)
```

Fixes confirmed afterwards:

- `perl -e` → `perl-ok 5.030003` (`libperl.dylib` load failure had been
  a trust rejection, not a missing file)
- `ruby -e` → `rubyok` (framework `.bundle` plug-ins now admit)
- `ps aux` inside the chroot prints the real host process table,
  including the iOS `VirtualMachine.xpc` guest process.

dyld `"Library not loaded"` errors inside the chroot can mean a CS
admission failure rather than a missing file — check `jbctl
trustcache info` membership before assuming a staging gap.
