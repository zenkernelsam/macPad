# devin-054-frontend — path-query ABI/causal review RESULT

**Doc:** `docs/evidence/devin-d054-path-query-independent-review-20261019.md`
**Sources:** `tccd_arm64e` `42c388e8` disasm; xnu-8792.81.2 source
(device kernel 8792.82.2 — minor drift flagged); D052 artifacts.
**Device/backend/Git:** untouched; backend D054 owns helper execution.

## ABI contract — source-verified

`proc_pidpath_audittoken(t*, buf, size)`: size ∈ [1024,4096] else
ENOMEM/EOVERFLOW+0; uses `val[5]=pid` + `val[7]=pidversion` with
`PIF_COMPARE_IDVERSION`; returns strlen or 0+errno. tccd `0x4b9d8`
callsite matches exactly (`x0=&dict target_token`, `0x400` buf). PAC:
dlsym'd fptr must go through `-undefined dynamic_lookup` (D052 v1
crash already proved raw-fptr faults).

## ENOENT has exactly one producer in this flow

`proc_pidpathinfo_internal`: NULL textvp / getwithvid /
**namecache walk** / ref_ext / recycled / stale pidversion / dead pid →
all **ESRCH**; permission-deny → EACCES→kernel-retry→ENODEV. **ENOENT is
only `vnode_lookup(resolved_path, vfs_context_current())`** — the
caller's-own fs context failing to find an *already namecache-resolved*
path.

**tccd is chrooted** (`launchdchrootexec … /var/mnt/rootfs … tccd` —
verified plist + binary `chroot` string). The helper's vnode path
(`/private/preboot/…` or `/var/jb` spelling — same vnode) doesn't exist
inside the macOS rootfs → ENOENT. This is mechanism-consistent THEORY —
the alternative same-class producer (stale namecache entry for a path
gone globally) can't be excluded without the resolved path.

**Corollaries:** ENOENT excludes missing-textvp/stale-token (those are
ESRCH); WS's own `/System/…` exec path resolves inside the rootfs → its
failure was stage-1 `#103`, consistent with R002; existence-only lookup
(`vnode_put(nvp)`, no vnode compare) means an iOS sender at a path that
coincidentally exists in the rootfs could pass stage-2 spuriously.

## Minimal control matrix (zero TCC traffic)

self byToken+byPID → predicted success + real path; `proc_pidpath` on a
`/var/jb` daemon pid + a platform pid → cross-section; stale-token →
ESRCH negative control; **decisive**: helper `chroot("/var/mnt/rootfs")`
then `proc_pidpath(same pid)` → ENOENT = direct proof the caller
namespace produces it (euid=0 needed; helper already runs root). Record
the resolved path spelling — it discloses which namespace leg failed.

## Bounds kept

ENOENT ≠ bind/trust (either direction); self-success ≠ exclusion of
server token races; `count=0` ≠ attribution bail without the correlated
ERROR pair; canonical install still not justified — if caller-context
proves out, the fix is making the sender's path resolvable *inside
tccd's root*, not moving the binary.

(handover to codex now)
