# D055 backend result — caller-namespace producer proven

2026-10-19. Executed `coordination/_relay/devin-055-backend-task.md`.
Zero TCC traffic; one helper exec, forked-child namespace transition,
no second exec/admission. Desktop `ready` before/after (WS 10447).

## Decisive result — same live identity, only fd_rdir differs

`macws_d055_nsprobe` (`/var/jb/tmp` exec, uid 0, parent pid 29644,
token held alive): identical `proc_pidpath`/`proc_pidpath_audittoken`
queries on the parent's unchanged token:

```
native parent + native CTRL child -> rc=162,
  path=/private/preboot/…/dopamine-…/procursus/tmp/macws_d055_nsprobe
CHROOT child (chroot("/var/mnt/rootfs") rc=0, chdir rc=0,
  stat(/private/preboot)=ENOENT, same parent token)  -> ENOENT
child's own token from chroot                        -> ENOENT
dead-pid negative control                            -> ESRCH(3)
parent re-verify post-children                        -> rc=162 again
```

**MATCHED success→ENOENT under chroot: the caller namespace is the
producer of the `(#2)` error for this API/build.** tccd's chroot
(`/var/mnt/rootfs`) cannot express the global path the kernel builds
for a `/var/jb`-exec'd sender — caller-context `vnode_lookup`
(xnu-8792 error map, D054-FE) emits ENOENT → attribution-chain bail →
empty `x23`.

## Boundaries (per packet)

- Proven for this API/build only — not universal for every tccd
  ENOENT; chroot senders' stage-1 `#103` is a separate live gate.
- Not claimed: dead-vnode universality, all-senders-fail, or a
  canonical-install fix. Recorded alternative within same class
  (stale global namecache) cannot be excluded without a resolved-path
  witness — the chroot A/B is the witness now landed.
- No TCC requests sent; no setters/trust/production changes;
  no markers; credentials env-only.

## Artifacts

`analysis/devin-d055/` — source `c8663108`, unsigned `3645c717`,
device-signed `84bc561f`, `d055-device-capture.txt` (verbatim two
runs, identical outcome). Device: helper/binaries removed, no jobs,
no procs, no residue. Health post: `ready seq 3989`.

## Next minimal experiment (documented, not executed)

A sender passing BOTH gates needs launchd responsibility registration
+ an exec path expressible inside `/var/mnt/rootfs` — i.e. an owned
helper placed under the rootfs and admitted via the project trust
tool (trust-list write — next packet's decision). Then stage-3
answers whether a populated `auth_*` verdict or the next named error
emerges.

Docs: `docs/evidence/devin-d055-namespace-control-20261019.md`.
