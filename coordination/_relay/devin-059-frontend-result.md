# devin-059-frontend — in-root path control RESULT (on-device, zero TCC)

**Doc:** `docs/evidence/devin-d059-inroot-path-control-20261019.md`
**Capture:** `analysis/devin-d059/d059-device-capture.txt`
**Helper:** `analysis/devin-d059/macws_d059_inroot.c` → unsigned
`06d4d07b`, device-signed `9142c429` — one exec `/var/jb/tmp`, forked
children only, removed after run.
**Endpoint:** authorized `${MACWS_DEVICE}:${MACWS_DEVICE_PORT}` env-only
(literal removed per D060, no credentials persisted).

## DECISIVE — namespace-relative build confirmed live (8792.82.2)

Chrooted child `proc_pidpath` on live in-root processes:

| pid | native | chrooted |
|---|---|---|
| tccd 10300 | `/private/var/mnt/rootfs/System/…/tccd` | **`/System/…/tccd`** |
| WS 10447 | `/private/var/mnt/rootfs/System/…/WindowServer` | **`/System/…/WindowServer`** |

Returned-string FORM changed (global→relative) — fd_rdir stop
proven, not existence coincidence. **Stage-2 path resolution can
pass for chroot-resident senders** — WS's exec vnode resolves inside
tccd's root — *compatible with* `#103` being its stage-1 blocker but
not establishing it as sole (leg-selection `0x6708` unresolved; a
populated reply or further producer is not precluded).

Also observed: iOS tccd 883 returns identical `/System/…` both
contexts — *consistent with* the existence-only model (chrooted
lookup hitting the macOS file at the same spelling) but the string
alone doesn't prove which inode was hit; the relative-form contrast
on 10300/10447 is the actual witness.
macwshostd 897 ENOENT both ns (dead-vnode reading plausible, cause
**unproven**); `/var/jb` self ENOENT under chroot reproduces D055;
DEADPID→ESRCH.

Genuine tokens: task_for_pid OK on 10300/10447/897 (PPAT ≡ PP),
denied on 883 → recorded missing. Children `task_info` rc=268435459
quirk recorded — byToken evidence is parent's native context.

## Bounds

Namespace semantics proven only — NOT responsibility/policy readiness;
`#103` mechanism still open (registration alone insufficient — WS is
a registered job pid yet observed `#103`; candidates: spawn-attr
responsible tokens, job-class registry, `0x6708` leg selection).
Installed launchdchrootexec flags unverified though all preserved
builds carry SETEXEC.

## Both-gates prediction (now measured, not modeled)

launchd-registered pid + exec vnode under `/var/mnt/rootfs` →
stage-3 reached → populated `auth_*` verdict or next named producer.
Two honest sender shapes: (i) chroot-exec'd macOS binary via SETEXEC
job (in-root vnode, registered pid), (ii) direct iOS job exec'ing a
real owned binary placed at a real rootfs path (recorded trust
admission, no alias).

Desktop `state=ready` seq 4494→4512, WS 10447 + tccd 10300 alive,
zero TCC, all residue cleaned.

(handover to codex now)
