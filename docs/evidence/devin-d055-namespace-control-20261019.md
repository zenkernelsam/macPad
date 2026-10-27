# D055 — same-process namespace control: ENOENT producer proven

2026-10-19, packet `coordination/_relay/devin-055-backend-task.md`.
Zero TCC traffic; one packet-owned helper, one exec, forked children
only — **no second exec, no chroot binary admission needed**. No
setters/policy/identity/trust workarounds. Desktop `ready` seq
3987→3989, WS 10447 unchanged.

## Mechanism under test

Per the xnu-8792 `proc_pidpathinfo_internal` error map (D054 frontend
review): `vn_getpath_fsenter` builds the vnode's *global* namecache
path, then `vnode_lookup(path, 0, &nvp, vfs_context_current())`
**re-resolves it in the CALLER's namespace** → that re-lookup is the
sole ENOENT producer; stale/dead/no-textvp all yield ESRCH instead.

## Design — matched A/B on one live identity

Parent (`/var/jb/tmp` exec, uid 0, pid 29644) keeps its own token
alive; two sequential forked children run the **identical** query set
(`proc_pidpath(pid)` + `proc_pidpath_audittoken(token)`) on that same
parent identity:

- **CTRL-CHILD** — original namespace (sanity: token alive → resolve).
- **CHROOT-CHILD** — calls `chroot("/var/mnt/rootfs")`+`chdir("/")`
  in-process, then the identical calls — tccd's caller context
  replayed with no exec.
- **DEADPID** — `proc_pidpath(reaped child)` → expect ESRCH, verifies
  the errno classes separate at runtime.
- **PARENT-END** — re-run after children: identity/token unchanged.

Everything the chrooted child touches was pre-bound in the parent
phase (`chroot` via a guaranteed-ENOENT dummy call, `chdir`,
`getcwd`, `stat`, `strerror`, `printf` family); the child exits
instead of restoring.

## Results — verbatim (`d055-device-capture.txt`)

```
PARENT  PID=29644  PP/PPAT(self) -> rc=162
  /private/preboot/CFD92CED…/dopamine-IWpVUH/procursus/tmp/
  macws_d055_nsprobe
CTRL-CHILD (native ns) on parent identity:
  PP/PPAT -> rc=162 -> same path
CHROOT-CHILD pid=29646: chroot_rc=0 chdir_rc=0 cwd=/
  stat(/private/preboot) -> errno=2   (bind mount absent in ns)
  PP/PPAT(parent identity) -> rc=0 errno=2 ENOENT
  PPAT(child's own token)  -> rc=0 errno=2 ENOENT
DEADPID PP -> errno=3 ESRCH
PARENT-END: token byte-identical, PP/PPAT -> rc=162 again
```

## Verdict

**MATCHED success→ENOENT under in-process `chroot` — the caller
namespace is the producer** of the D052 `(#2) ENOENT` for this
API/build. Same live token, same exec vnode, only `fd_rdir` changed.

The D052 server's `(#2)` is now explained mechanically: tccd runs
chrooted into `/var/mnt/rootfs`; the helper's vnode resolves globally
to `/private/preboot/…/procursus/…` which does not exist under that
root → the caller-context `vnode_lookup` re-resolution emits ENOENT →
`Failed to create attribution chain` → empty `x23` reply.

**Boundary kept per packet:** matched success→ENOENT proves the
caller-namespace mechanism *for this API/build* — it does not prove
every tccd ENOENT shares this cause, and it does not alone explain
the chroot senders' `#103` (stage-1 responsibility is a separate
gate). Self-success in the native namespace does not exclude
server-side pidversion races for other cases — not claimed.

Also recorded:
- `DEADPID → ESRCH(3)` vs `ENOENT(2)` — error classes discriminate at
  runtime as the xnu map predicts.
- `CHROOT-SELF` ENOENT — the child's own exec vnode (same `/var/jb`
  file) is equally unnameable from inside the chroot — consistent.
- No universal "all senders fail" claim: a sender exec'd from a path
  that also exists inside `/var/mnt/rootfs` would pass stage 2.

## What this implies for the attribution chain (server)

| sender shape | stage-1 responsibility | stage-2 path-in-tccd-view |
|---|---|---|
| chroot-forked (launchdchrootexec child) | #103 fail | (would pass — path inside chroot) |
| iOS launchd job on `/var/jb` bind | passes | #2 ENOENT |
| iOS daemon w/ dead exec vnode | (passes) | #2 ENOENT (own class) |

To ever see a populated reply, a sender must satisfy BOTH — currently
nothing does. The window-native clients (WS etc.) sit in row 1: their
exec path *would* resolve inside tccd's root, but they never reach
stage 2 — the `#103` responsibility gate is the load-bearing wall for
them.

## Exact files / hashes / hygiene

- `analysis/devin-d055/macws_d055_nsprobe.c` `c8663108`
- unsigned build `3645c717`; device-signed `84bc561f`
- `analysis/devin-d055/d055-device-capture.txt` — full verbatim run
- Cleanup: helper removed from `/var/jb/tmp`; no trustcache entries
  ever added (path-trusted location only); no jobs; no leftover
  processes; crash-report sweep done.
- Desktop health: `state=ready producer=10447` before & after.

## Next minimal producing-layer experiment

Constructing a sender that passes both gates now requires a
**launchd-registered** process exec'd from a path expressible inside
tccd's root — i.e. a launchd job binary placed under
`/var/mnt/rootfs` (which also needs the project's trust admission for
that inode). That is the smallest remaining input; whether the then-
reached stage-3 (identity/policy eval) produces a verdict dict or the
next named error is the open question it would answer.
