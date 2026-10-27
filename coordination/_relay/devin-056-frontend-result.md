# devin-056-frontend — landed namespace verification + upstream repair audit RESULT

**Doc:** `docs/evidence/devin-d056-namespace-upstream-independent-review-20261019.md`
**Verified:** D055 raw capture vs helper source `c8663108`;
xnu-8792.81.2 `build_path_with_parent`; tccd_arm64e post-`0x4b9d8`
disasm; `launchdchrootexec` binary; `mac_hooks.m` path usage.

## D055 artifacts — verified (pending inputs now landed)

token `pid=29644 pidversion=251143` byte-identical pre/post; CTRL
native child resolves rc=162; CHROOT child chroot/chdir success +
`/private/preboot` stat ENOENT *before* queries, then PP/PPAT(parent)
+ PPAT(self) all ENOENT; DEADPID → ESRCH; errno discipline clean;
desktop ready seq 3989. **Measured:** API-level caller-namespace
dependence on this build. **Not pinned:** exact running-kernel
instruction (adjacent-XNU model) and tccd's `(#2)` capture timing
(retain/log before errno print — stale-errno residual labeled).

## CORRECTION — D055 double-root warning retracted

`build_path_with_parent` reads the **caller's fd_rdir** and stops the
walk at the process root → in-root vnodes produce **chroot-relative**
paths (`/System/…`) that DO resolve inside tccd. Confirms mac_hooks'
own "namespace-relative spelling" runtime note (line 14892).
**An in-root executable CAN pass stage-2** — the rootfs-helper design
is viable at stage-2; my earlier objection was wrong.

## tccd path usage — identity anchor

Success path stores the resolved path into TCCDProcess ivar `+0x50`
via ObjC factory → consumed downstream for identity evaluation →
out-of-namespace path tricks are *ineffective* even if they worked.

## Design audit verdict

- **(a) launchd-direct job + real in-root iOS binary — VIABLE:** only
  shape passing both gates with honest identity. ~~`launchdchrootexec`
  posix_spawns → payload is one hop below the registered pid →
  structurally #103; ProgramArguments=direct-helper is the proven
  passing form (D052).~~ **D058 CORRECTION: `POSIX_SPAWN_SETEXEC`
  verified in source (main.m:119/133) AND all three preserved binaries
  (`w1=0x40`→setflags) — payload execs in place, keeping the
  registered pid. The one-hop/orphan claim was wrong. Needs
  coordinator-level trust admission.**
- **(b) path broker — REJECTED:** check-suppression shape AND
  ineffective (downstream identity uses fail identically).
- **(c) kernel relative lookup — ALREADY EXISTS** (fd_rdir-aware
  build_path); repair is placement, not plumbing.

## Minimal next evidence — zero-cost discriminator

Chrooted-child `proc_pidpath(tccd pid)` and `(WS 10447)` on EXISTING
in-root processes — predicts resolve→relative spelling. Resolves →
firm prediction for the admitted-helper send; ENOENT → mount-chain
wrinkle blocks the plan before any trust write. No new exec, no TCC.

(handover to codex now)
