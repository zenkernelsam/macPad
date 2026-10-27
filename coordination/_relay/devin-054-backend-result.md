# D054 backend result — path-query control, ENOENT attributed to context

2026-10-19. Executed `coordination/_relay/devin-054-backend-task.md`.
Zero TCC traffic; read-only helper + stock-binary observation; no
deploy/restart/marker/source/Git changes. Desktop `ready` throughout
(seq 3863→3895, WS 10447).

## Verdict

`proc_pidpath_audittoken (#2) ENOENT` is **not** a bind defect, trust
defect, or universal kernel cause — directly observed otherwise:

- `proc_pidpath` AND `proc_pidpath_audittoken` on a `/var/jb/tmp`-
  exec'd self resolve cleanly (rc=164 → `/private/preboot/…/procursus/
  tmp/…` backing path). Same for a live hold pid queried natively.
- Platform pid 864 resolves to `/usr/libexec/UserEventAgent`.
- macwshostd 897 → ENOENT on both calls — separate cause: dead exec
  vnode (post-deploy inode replacement), recorded as its own class.
- `/private/preboot` does not exist inside the chroot namespace
  (`launchdchrootexec /bin/ls` → ENOENT).

Producer for the D052 sender's ENOENT: **tccd's chrooted filesystem
view cannot name the iOS-side dopamine-bind executable path** —
stage-2 attribution failure, context-dependent. Whether the ENOENT
is emitted kernel-side (namespace-aware path build) or at tccd's
post-call path validation is a labeled sub-THEORY — same layer
either way, not sole-cause-claimed.

## Attribution model (two gates, both server-side)

1. responsibility registration — chroot-forked senders fail (#103)
2. sender exec path resolvable in tccd's chroot view — iOS `/var/jb`
   senders and dead-vnode daemons fail (#2 ENOENT)

Both gates fail differently; both end at the same unconditional empty
`x23` reply (D050). No sender in the current deployment can pass both.

## Corrections / boundaries absorbed

- D053's interpose catch recorded: `sender_auid=501` was
  `audit_token_to_auid` interposition, not genuine auid.
- ENOENT not attributed to bind/trust by inference — excluded by
  direct observation.
- task_for_pid asymmetry: works on launchd-managed daemon, denied on
  platform + ad-hoc pids → platform token control recorded MISSING,
  never synthesized.
- No send repeated; this round sent zero TCC requests.

## Next minimal experiment

One send from a launchd-registered sender whose executable sits inside
`/var/mnt/rootfs`'s view — requires admitting an owned helper into the
chroot trust list (`macws_boot_trust --readd`, recorded admission);
the chroot `/tmp` copy could not exec untrusted. That is the sole
remaining gate between the observed state and a populated verdict or
the next named producer.

Artifacts: `analysis/devin-d054/` (source `d8245b71`, unsigned
`3e806cfa`); evidence `docs/evidence/devin-d054-path-query-control-
20261019.md`. Cleanup verified — no files/procs/jobs remain.
