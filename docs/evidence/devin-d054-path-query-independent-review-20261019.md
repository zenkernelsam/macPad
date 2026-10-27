# D054 — independent ABI/causal review of the ENOENT producer

Date 2026-10-19, packet `coordination/_relay/devin-054-frontend-task.md`.
**Read-only. Sources: `tccd_arm64e` disasm (`42c388e8`), xnu-8792.81.2
source (device runs 8792.82.2 — minor-rev drift noted), D052 artifacts.
No device, no resend, no backend contact.**

## 1. Wrapper ABI — verified from `libsyscall/…/libproc.c:264-309`

```c
int proc_pidpath(int pid, void *buf, uint32_t size);
int proc_pidpath_audittoken(audit_token_t *t, void *buf, uint32_t size);
```

- size must be `[PROC_PIDPATHINFO_SIZE=MAXPATHLEN=1024,
  MAXSIZE=4*MAXPATHLEN=4096]`; else `errno=ENOMEM|EOVERFLOW`, return 0.
- `_audittoken` uses only `val[5]=pid`, `val[7]=pidversion`; calls
  `__proc_info_extended_id(PIDINFO, pid, PROC_PIDPATHINFO=11,
  PIF_COMPARE_IDVERSION, idversion, 0, buf, size)`; returns `strlen(buf)`
  or `0` w/ errno.
- **tccd `0x4b9d8` callsite matches exactly**: `x0=&target_token`
  (dict-supplied token), `x1=buf`, `w2=0x400`. A proposed helper query
  with the same ABI is faithful; the arm64e PAC contract for the fptr is
  the sole risk (D052 v1 crash already proved raw-dlsym-calling faults —
  use `-undefined dynamic_lookup` binding, not a hand-called fptr).

## 2. Kernel error map — `bsd/kern/proc_info.c:1362-1435` (the decisive part)

`proc_pidpathinfo_internal(p)`:

| step | failure → errno |
|---|---|
| `p_textvp == NULLVP` | **ESRCH** |
| `vnode_getwithvid(tvp,vid)` fails | **ESRCH** |
| `vn_getpath_fsenter(tvp,buf)` — namecache path build | **ESRCH** |
| `vnode_ref_ext` | **ESRCH** |
| `vnode_lookup(buf,0,&nvp,vfs_context_current())` — **caller-context re-lookup** | **ENOENT** (unless recycled/unmounting → ESRCH; EACCES → retried under `vfs_context_kernel`, repeat EACCES → ENODEV) |
| pid stale/missing/`PIF_COMPARE_IDVERSION` mismatch (line 2076) | **ESRCH** |
| `proc_security_policy` | EACCES/EPERM-class |

**`#2 ENOENT` has exactly one producer**: the namecache already resolved
a global path for the vnode (`vn_getpath_fsenter` succeeded — the vnode
*is* nameable), but re-resolving that path inside **the caller's**
vfs context returned ENOENT. Stale tokens, dead procs, missing textvp,
and trust/namecache gaps all produce ESRCH — **ENOENT actually excludes
the "no textvp"/stale-token class**, narrowing further than "ambiguous
errno" implies.

## 3. Caller context — why tccd is the unusual caller

tccd's job: `launchdchrootexec 0 0 /var/mnt/rootfs …/tccd system` —
chrooted into the macOS rootfs. `vfs_context_current()` there resolves
absolute paths under `/var/mnt/rootfs`. The helper's vnode namecache
path is its real iOS path (`/private/preboot/…/procursus/tmp/…` or the
`/var/jb/…` spelling — same vnode either way, per D053) — **neither
exists inside the macOS rootfs → ENOENT.** This is a mechanism-consistent
reading of the observed error, not yet proof: an alternative in the same
errno class is a stale namecache entry whose path is gone *globally*
(e.g., replaced post-namecache) — cannot be excluded without seeing the
resolved path.

Corollary that sharpens the picture: the chroot-exec'd **WindowServer's
own** exec path (`/System/…` under `/var/mnt/rootfs`) resolves *inside*
tccd's root — stage-2 would pass for WS; its failure sits at stage-1
responsibility (`#103`), matching R002. A sender exec'd from an iOS path
that happens to exist in the rootfs could pass stage-2 spuriously — the
lookup is existence-only (`vnode_put(nvp)` — no vnode comparison).

## 4. Minimal control matrix (bounded, zero TCC traffic)

| probe | expected if caller-context theory holds | if it fails |
|---|---|---|
| self `proc_pidpath_audittoken(&own)` + `proc_pidpath(getpid())` | success, real `/private/preboot/…` or `/var/jb/…` path | ENOENT→ vnode-path issue is real sender-side; ESRCH → namecache/textvp |
| `proc_pidpath(<live /var/jb daemon pid>)` | same success | generic /var/jb vnode property |
| `proc_pidpath(<platform daemon pid>)` | success + canonical path | sanity baseline |
| stale token (exited pid's saved token) | ESRCH — proves pidversion branch | confirms error discrimination |
| **self-chroot A/B**: helper `chroot("/var/mnt/rootfs")` then `proc_pidpath(same live pid)` | ENOENT → **direct proof** the caller namespace is the producer | success → theory refuted; reopen vnode-side analysis |

The self-chroot repro is the single most discriminating step — it
replays tccd's exact caller context locally with no server contact.
`chroot` requires euid=0 (helper ran root — feasible); it is read-only
on the caller's own fs view.

## 5. What this does NOT establish (preserved bounds)

- ENOENT → caller-context lookup failure is source-level; that tccd's
  chroot is *the* divergent context remains THEORY until the self-chroot
  repro or a resolved-path witness lands. Backend's "Dopamine bind /
  trust-admitted-exec" theories are not confirmed and not required —
  ENOENT alone carries neither.
- `count=0` EMPTY still has ≥4 producers; the correlated ERROR pair is
  what attributes this instance.
- Self-success does not exclude server-side token/pidversion races —
  only narrows to "if self resolves, vnode is nameable globally".
- Canonical-path installation is not a justified fix yet: if
  caller-context is proven, the fix direction is making the sender's
  exec path *exist within tccd's root* (or normalizing tccd's view),
  not relocating the sender binary.
- `raw1`/`impl_rc=0` remains collapsed; observer still undeployed;
  `sender_auid=501` remains interpose-forged.
- xnu-8792.81.2 source vs device `8792.82.2` kernel — minor-rev drift;
  error-map structure likely stable, exact line claims could shift.

## 6. Backend D054 alignment

Their plan (self + optional platform/rootless controls, no resend) is
consistent; the additions worth carrying: self-**chroot** A/B (the
decisive probe), a stale-token ESRCH negative control, and recording
the *resolved path string* on success (its spelling discloses which
namespace leg failed — `/var/jb` vs `/private/preboot`).
