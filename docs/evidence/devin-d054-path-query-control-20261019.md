# D054 — executable-path query control (zero TCC traffic)

2026-10-19, packet `coordination/_relay/devin-054-backend-task.md`.
Read-only path-query helper, no TCC requests, no WS restart, no
identity/policy/marker/source changes. Desktop observed before/after
(`ready`, WS 10447, seq 3863→3895).

## Purpose

Pin the actual producer of `proc_pidpath_audittoken() (#2) ENOENT`
seen by tccd for the D052 iOS sender (pid 27108). Explicitly NOT
assumed: bind-path defect, trust defect, universal kernel cause.

## ABI verification (evidence discipline)

tccd 22F82 arm64e (`42c388e8…`), `0x10004b9d8`:

```
x0 = &audit_token   (x21; 32-byte token copied to sp+0x70)
x1 = path buffer    (sp+0x70)
w2 = 0x400          (bufsize)
bl 0x100055280      = proc_pidpath_audittoken stub
cmp w0,#0 ; b.le    -> error path (logs errno)
```

→ `int proc_pidpath_audittoken(const audit_token_t *, void *,
uint32_t)`, return = path length, `<=0` + errno on failure. Matches
the classic `proc_pidpath(pid, buf, size)` convention.

## Helper

`analysis/devin-d054/macws_d054_pathprobe.c` (sha256 `d8245b71`,
final run build unsigned `3e806cfa`); arm64e, `-undefined
dynamic_lookup`, `weak_import` on all three SPIs — direct calls only,
no dlsym'd function pointers (D052-v1 PAC fault documented). Signed
`ldid -S`, exec'd from `/var/jb/tmp` — the validated owned location.
One-shot, `alarm(15)` bound.

## Results matrix (device, verbatim)

```
SELF (/var/jb/tmp-exec'd, pid 28650, sshd-context):
  EXE dev=16777222 ino=124817
  /var/jb -> /private/preboot/CFD92CED…/dopamine-IWpVUH/procursus/
  proc_pidpath(getpid)            rc=164 path=/private/preboot/…/
                                  procursus/tmp/macws_d054_pathprobe
  proc_pidpath_audittoken(self)   rc=164 path=SAME

PLATFORM (UserEventAgent 864, /usr/libexec):
  proc_pidpath(864)               rc=27 path=/usr/libexec/UserEventAgent
  task_for_pid(864)               rc=5 DENIED -> token unobtainable
                                    (recorded missing, not synthesized)

JBDAEMON (macwshostd 897, /var/jb/usr/macOS/bin):
  proc_pidpath(897)               rc=0 errno=2 ENOENT
  task_for_pid(897)               rc=0 OK (root, launchd-managed)
  token = {auid=-1 euid=0 pid=897 pidversion=3307}
  proc_pidpath_audittoken(897)    rc=0 errno=2 ENOENT

LIVE HOLD (/var/jb/tmp exec'd, pid 28769, queried by native caller):
  proc_pidpath(28769)             rc=164 resolves -> preboot path
  task_for_pid(28769)             rc=5 DENIED (ad-hoc pid)

CHROOT filesystem view (stock /bin/ls, no helper exec needed):
  ls /private/preboot/            -> ENOENT (mount absent in chroot)
```

## Producer resolution

**FACT:**
1. `/var/jb`-exec'd vnodes resolve fine for a native caller —
   `proc_pidpath`/`_audittoken` return the preboot backing path.
   The D052 "bind path" THEORY for ENOENT is disproven as a generic
   cause.
2. macwshostd 897 ENOENT = a genuinely dead exec vnode (deployment-
   inode replacement — file replaced after exec; consistent with the
   documented deploy landmine). A *distinct* cause, same errno.
3. The dopamine procursus mount is **absent inside the chroot
   namespace** (`ls /private/preboot` → ENOENT under
   `launchdchrootexec`). tccd is chrooted into `/var/mnt/rootfs`.

**Verdict on the D052 ENOENT:** the producing layer is the
*tccd-side filesystem context* — the kernel's resolved backing path
for a `/var/jb`-exec'd sender binary cannot be named/validated in the
chrooted daemon's mount view → ENOENT → `Failed to create attribution
chain` → empty `x23`. Context-dependent evidence: this explains the
27108 failure without asserting whether the kernel emits ENOENT inside
`proc_pidpath_audittoken` for chrooted callers or tccd validates the
returned path itself (the log attributes the errno to the function
call; the two sub-mechanisms are indistinguishable from this data and
are left as a labeled sub-THEORY).

NOT claimed: universal kernel defect, bind defect, or trust defect —
all three are now excluded by direct observation, not assumed.

## Updated attribution model (attribution chain, server-side)

| stage | gate | who fails |
|---|---|---|
| 1 | responsibility registration | chroot-spawned senders (#103) |
| 2 | `proc_pidpath_audittoken` — sender exec path expressible in tccd's chroot view | iOS `/var/jb` senders (#2 ENOENT); dead-vnode daemons (#2) |

A sender that passes BOTH needs launchd responsibility registration
AND an executable on a mount present inside `/var/mnt/rootfs`'s view.

## Next minimal experiment

Execute the same `TCCAccessRequest` once from a sender whose exec
binary lives **inside the chroot filesystem view** AND is
launchd-registered: the missing input is a trusted binary under
`/var/mnt/rootfs` — the chrooted `/tmp` copy did not exec (untrusted,
no output — consistent with D048's `/tmp` kill). Admission of an
owned helper into the chroot trust list (recorded `--readd` admission)
is the sole missing step, deferred to the next packet since it
requires the project's trust-list write.

Secondary cheap check for the sub-THEORY: the chrooted helper exec,
once admitted, calling `proc_pidpath_audittoken` on a live /var/jb
pid — ENOENT reproduces → kernel-side namespace failure; resolves →
tccd's post-syscall stat is the site.

## Hygiene

- Helper binary removed from `/var/jb/tmp` and `/var/mnt/rootfs/tmp`;
  no trustcache entries added (helper ran from the path-trusted
  location only); crash .ips cleaned; hold processes exited;
  d052 job already booted out.
- task_for_pid asymmetry recorded: succeeds on launchd-managed
  daemon (897) but denied (rc=5) on ad-hoc shell pid and on platform
  864 — context fact, not generalized.
- Zero TCC requests were sent this round.
