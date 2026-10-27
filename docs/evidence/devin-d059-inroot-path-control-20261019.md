# D059 — in-root process path control (zero TCC, on-device)

2026-10-19, packet `coordination/_relay/devin-059-frontend-task.md`.
Authorized endpoint `${MACWS_DEVICE}:${MACWS_DEVICE_PORT}` (sshpass `-e`,
env-only credentials — endpoint literal removed per packet D060, no
credentials persisted). One helper exec from `/var/jb/tmp`, forked
children only — no second exec, no TCC traffic, no deploy/restart/
trust writes. Desktop `state=ready` seq 4494→4512 before/after.

Helper: `analysis/devin-d059/macws_d059_inroot.c` → unsigned host build
`06d4d07b…`, device-signed `9142c429…`. Raw capture:
`analysis/devin-d059/d059-device-capture.txt`.

## Result — namespace-relative path build CONFIRMED on device

xnu-8792.**82.2**, same live processes, only `fd_rdir` differs:

| pid | role | native caller | chrooted child |
|---|---|---|---|
| 10300 | macOS tccd (rootfs-exec'd) | `/private/var/mnt/rootfs/System/…/tccd` | **`/System/…/tccd`** rc=60 |
| 10447 | WindowServer (rootfs-exec'd) | `/private/var/mnt/rootfs/System/…/WindowServer` | **`/System/…/WindowServer`** rc=86 |
| 883 | iOS tccd (platform) | `/System/…/tccd` | `/System/…/tccd` |
| 897 | macwshostd (replaced exec vnode) | ENOENT | ENOENT |
| self | `/var/jb/tmp` helper | preboot path rc=161 | **ENOENT** (D055 reproduces) |

Facts established:

1. **In-root vnodes resolve to chroot-relative spellings** — the
   returned string changes *form*: `/private/var/mnt/rootfs/System/…`
   (native) vs `/System/…` (chrooted). The relative spelling is direct
   evidence of the fd_rdir stop in `build_path` — not an existence
   coincidence (the global spelling would have been returned
   unchanged if it merely resolved inside the root).
2. **Existence-only semantics consistent — but spelling is not proof**:
   iOS tccd (883) returns the same `/System/…/tccd` in both contexts.
   Consistent with the chrooted lookup resolving the **macOS** file at
   that spelling (existence-only check; per the xnu source the looked-
   up vnode is dropped un-compared). However the identical returned
   string alone does not prove which inode the lookup hit or the exact
   lookup implementation — the read holds via the source model plus the
   in-root relative-form contrast on 10300/10447, not via the string.
3. **`/var/jb` vnode class still ENOENTs under chroot** — D055
   reproduced identically (child self-PPAT → ENOENT while
   `stat(/private/preboot)` = ENOENT).
4. **macwshostd 897** ENOENT in both namespaces — consistent with the
   deploy-replacement dead-vnode reading, **unproven**: any stale
   namecache→missing-path condition shares this errno; its producer
   was not isolated on device.
5. **Genuine tokens**: `task_for_pid` succeeded on 10300/10447/897
   (launchd-managed) — `PPAT` returned identical paths to `PP`.
   Denied on platform 883 → recorded MISSING, not synthesized.
   (Children's `task_info` on those ports failed rc=268435459 —
   fork-related port quirk, recorded; byToken evidence therefore
   comes from the parent's native-context results.)
6. DEADPID → ESRCH — errno classes still discriminate.

## What this proves / does not prove

**Proven (measured, this build):** a chrooted caller can path-resolve
an in-root sender's exec vnode — stage-2's lookup CAN pass for
chroot-resident senders. WS's own exec vnode resolves to
`/System/…/WindowServer` inside tccd's root — **compatible with** WS's
`#103` being a stage-1 bail, but NOT establishing that stage-1 is the
sole remaining blocker: the flag-gated leg selection (`0x6708`) is
unresolved, so whether WS's request even traversed the responsibility
leg is unverified.

**Not proven:** both-gates readiness (a passing stage-2 prediction
does not establish stage-1 will pass for any candidate sender, nor
that a populated reply follows); launchd responsibility/policy
readiness; whether tccd's `#2` errno came
from the kernel leg vs tccd's own post-call validation (the call
itself fails before any path exists for the `/var/jb` case, so the
kernel leg remains the parsimonious producer — still labeled);
all-senders universality (existence collisions like 883 pass
spuriously).

## Consequence for the two-gates design

The honest sender shape now fully specified by direct observation:

- stage-1: registered launchd job pid — with `POSIX_SPAWN_SETEXEC`
  verified in all preserved `launchdchrootexec` builds, a chroot
  payload keeps the registered pid (installed-binary identity still
  unverified); OR a direct `ProgramArguments` iOS helper job.
- stage-2: exec vnode under `/var/mnt/rootfs` — either a chroot-exec'd
  macOS binary OR a real iOS-ABI helper file placed under the rootfs
  mount (real file, recorded trust admission — no alias).

Stage-2 side is measured; stage-1 remains modeled: a launchd-
registered sender exec'd from a genuine in-root inode is *predicted*
to clear stage-2; whether it then produces a populated `auth_*` reply
or the next named producer depends on the unresolved responsibility
leg — not established by this experiment. Remaining unknowns:
installed `launchdchrootexec` flags on device; whether iOS launchd
job registration produces the responsibility entry the `0xb6`
provider queries (WS's `#103` despite job-pid registration is the
open contradiction — registration alone is NOT responsibility proof);
which leg each sender's request actually traverses (`0x6708` flag).

## Hygiene

Helper removed from `/var/jb/tmp`; no jobs/procs/procs residue;
no trust-list, plist, or production changes; zero TCC requests.

(handover to codex now)
