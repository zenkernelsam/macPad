# D056 — namespace evidence verification + upstream repair audit

Date 2026-10-19, packet `coordination/_relay/devin-056-frontend-task.md`.
**Local-only audit of the landed D055 raw artifacts + independent
source/disasm review of the repair design space. No device, no TCC,
no backend contact.**

## 1. D055 artifacts — verified, pending-inputs now landed

`d055-device-capture.txt` vs `macws_d055_nsprobe.c` (`c8663108`):

- Parent pid 29644 token `val[5]=0x73cc=29644 val[7]=0x3d507=251143`;
  **byte-identical dump reprinted at PARENT-END** — token lifetime
  held across both children; parent alive throughout.
- CTRL child (native ns): PP+PPAT on parent identity → rc=162, same
  preboot path. Token valid across fork.
- CHROOT child: `chroot_rc=0 chdir_rc=0 cwd=/` and
  `stat(/private/preboot)=ENOENT` — namespace transition proven
  *before* the queries; then parent PP, parent PPAT, and child-self
  PPAT all → `rc=0 errno=2 ENOENT`.
- DEADPID → ESRCH(3) — errno classes discriminate at runtime as the
  xnu map predicts; ENOENT is not a dead-pid artifact.
- errno discipline identical to D054 (zeroed pre-call, captured
  immediately, path only on rc>0). Pre-bind phase for all child-
  touched symbols — no lazy load post-chroot.
- Cleanup verified: no procs, `state=ready seq 3989 WS 10447`.

**Verdict carried forward:** measured API-level caller-namespace
dependence on this build — same live identity, only `fd_rdir` differs.
What it does NOT pin: the exact instruction inside the running
8792.82.2 kernel (error map remains source-informed on adjacent
8792.81.2 text), and whether tccd's printed `(#2)` is captured
immediately post-call (its error path at `0x4ba90` retains/logs first;
stale-errno misattribution is a labeled residual, not excluded).

## 2. CORRECTION — retracting my D055 "double-root" warning

My warning assumed the resolved name is unconditionally the *global*
namecache path. It is not: `build_path_with_parent` (vfs_cache.c)
reads **`vfs_context_proc(ctx)->p_fd.fd_rdir`** — the caller's root —
and `vn_getpath_fsenter` chains through `vfs_context_current()`. The
walk **stops at the caller's root vnode** (`vp == proc_root_dir_vp` →
emits "/"), so:

| caller | vnode location | produced path |
|---|---|---|
| chrooted | in-root (`/var/mnt/rootfs/System/…`) | **chroot-relative** `/System/…` → lookup succeeds |
| chrooted | out-of-root (`/private/preboot/…`) | global → lookup ENOENT (measured) |
| native | anywhere | global → succeeds |

This matches mac_hooks' own runtime note (`mac_hooks.m:14892`): inside
the chroot `proc_pidpath` returns the *namespace-relative* spelling —
predicted exactly by fd_rdir-aware path build; the host-prefixed
spelling corresponds to native-context queries (or the subdir-root
case where fd_rdir isn't on the vnode's parent chain).

**Consequence:** a binary genuinely residing under `/var/mnt/rootfs`
resolves to `/tmp/X`-style relative paths inside tccd → stage-2 lookup
**can** pass for in-root executables. The backend rootfs-helper design
is viable at stage-2 — my earlier double-root objection was wrong and
is retracted. (D055-BK's own table already predicted this: "chroot-
forked sender would pass stage-2".)

## 3. What tccd does with the path — identity anchor, not a log

Disasm after `0x4b9d8` (tccd_arm64e): on rc>0 it builds two ObjC
objects (`bl 0x10005ade0`, `bl 0x10005ad00`, class-ref args) and
**stores one into the process object ivar `+0x50`**
(`[x23,#0x50]! ← x24`). The resolved path becomes the TCCDProcess's
executable identity for downstream evaluation. Any post-call
codesign/bundle/stat work on that path must ALSO succeed inside tccd's
root — so a path returned by out-of-namespace trickery buys nothing:
downstream uses would fail identically.

## 4. Repair design space — audited

**(a) Authentic launchd-owned spawn + real in-root executable —
VIABLE, the only complete design.** Both gates get honest inputs:

- stage-1 responsibility: ~~the sender must be a *direct* iOS-launchd
  job pid. `launchdchrootexec` **posix_spawns** its payload, so chroot
  payloads are one hop below the registered pid → structurally #103~~
  **D058 CORRECTION (2026-10-19): WRONG — `main.m:119` sets
  `POSIX_SPAWN_SETEXEC` (active, `setflags` at :133; the :114 line is
  commented dead code) and all three preserved binaries materialize
  `w1=0x40` (`SETEXEC`, suspend path `0xc0`) into
  `posix_spawnattr_setflags`: reviewed `eecf7c3c` UUID
  AEA33132-6DF7-3B62-8522-FEC402E8FE6B, staged `1004c790` UUID
  685002B3-ECBD-3BCD-9D32-6384F5875472, host-overlay `438c6482` UUID
  DC60AB9D-C4F8-3AC9-97F9-FD116D94320F.** SETEXEC execs the payload
  *in place* — the registered job pid BECOMES the macOS process; no
  orphan grandchild. Installed on-device binary identity is
  unverified without device access, but the flag predates every
  preserved build (in source since the initial import).
- stage-2: place a genuine iOS-ABI helper binary under
  `/var/mnt/rootfs` (a real file the project owns — NOT an alias):
  its vnode is in-root → tccd's query resolves it chroot-relative →
  lookup passes. Requires the project's trust-list admission for that
  inode — a coordinator decision, not something to shortcut.

**(b) Native-side identity/path broker — REJECTED.** Interposing
tccd's `proc_pidpath_audittoken` to return a real-but-foreign path is
(a) check-suppression of an explicit kernel existence gate (patch-
discipline violation shape), and (b) ineffective — §3 shows the path
is consumed as identity downstream; a global path fails every later
use in tccd's root anyway. A broker returning a *chroot-relative*
spelling would fabricate a path — forbidden. No variant is sound.

**(c) Kernel namespace-relative lookup — ALREADY EXISTS.** The
fd_rdir-aware `build_path` is exactly that mechanism; the API hands
tccd chroot-relative paths for in-root vnodes with no new machinery.
The repair is placement (a real file), not plumbing.

## 5. Minimal next missing evidence — discriminating, zero-cost

Before spending trust admission, the in-root resolution is testable
on **existing processes** with the same D055 helper pattern (one
exec, fork+chroot child — no new admission, no TCC):

- **Chrooted child calls `proc_pidpath(tccd's pid)`** (tccd exec'd
  under rootfs → in-root vnode) → prediction: **resolves** to
  `/System/Library/…/tccd` relative spelling. Same for WS pid 10447.
  - resolves → in-root stage-2 resolution confirmed → the
    trust-admitted helper experiment has a firm prediction (both
    gates pass → verdict dict or the next *named* producer).
  - ENOENT → a subtler mount/walk wrinkle (e.g. bind-mount parent
    chain missing fd_rdir on the path) → the rootfs-helper plan is
    blocked identically and must be re-thought before any admission.
- Stage-1 spawn-propagation boundary (documentation-complete for
  now): `responsibility_get_responsible_audit_token` resolves only
  direct launchd-registered pids — any design keeping a spawn hop
  (wrapper→payload) stays #103 regardless of stage-2. The d052 job
  shape (ProgramArguments = the iOS helper itself) is the proven
  passing form.

## 6. Boundaries kept

- No universal all-senders/EMPTY claims; `#103` vs `#2` remain
  separate gates; a chroot-resident sender is predicted — not
  observed — to pass stage-2.
- No alias/fake-return/check-suppression designs recommended; the
  only placement proposed is a real owned file + recorded trust
  admission (coordinator's call).
- Adjacent-XNU caveat retained: fd_rdir awareness is verified on
  8792.81.2 source; device 8792.82.2 could differ — the §5 probe
  measures it directly.
- Desktop untouched; backend D056 owns its parallel design work.

(handover to codex now)
