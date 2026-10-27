# devin-058-frontend — SETEXEC identity + corrected launch audit RESULT

**Doc:** `docs/evidence/devin-d058-launch-identity-independent-review-20261019.md`
**Audited binaries:** reviewed `eecf7c3c` (AEA33132-6DF7-3B62-8522-FEC402E8FE6B),
staged `1004c790` (685002B3-ECBD-3BCD-9D32-6384F5875472), host-overlay
`438c6482` (DC60AB9D-C4F8-3AC9-97F9-FD116D94320F) — all carry SETEXEC.

## SETEXEC — verified in source AND all preserved binaries

`main.m:119` active `spawn_flags = POSIX_SPAWN_SETEXEC`, `setflags` at
:133, `:114` commented dead code; disasm shows `w1=#0x40` default /
`0xc0` suspend path at the single setflags site in all three builds.
The flag predates every preserved build (initial import d4160fc).
**"posix_spawn ⇒ new pid" disproven — payload execs in place.** D056
"one hop / structural #103" claim retracted; docs corrected with
retained history. Installed on-device binary identity: presumed-
consistent, not proven (no device access this packet).

## Live contradiction now exposed — WS #103 unexplained

WS IS a direct iOS launchd job (UIKitApplication label →
launchdchrootexec) → with SETEXEC WS runs as the registered pid →
launchd-registration alone cannot explain its `#103`. Responsibility
must require more than registration (spawn-attr responsible tokens,
job-class registry, or the `0x6708` flag-gated leg) — **and d052's
"passed stage-1" is itself indirect** (a stage-2 ERROR may appear on a
leg that skipped responsibility). Gate-per-leg attribution open.

## Stage-2 corrected model — in-root predicted viable

fd_rdir-aware `build_path_with_parent`: chrooted caller + in-root
vnode → chroot-relative `/System/…` spelling → lookup resolves.
Source-verified on 8792.81.2; device drift caveat retained → needs
the control below.

## Minimal next evidence (proposed, zero-TCC/zero-exec pattern)

Chrooted child of the D055 helper shape: `proc_pidpath(tccd_pid)` and
`(WS 10447)` — predicted `/System/…` **relative spelling** (form of the
returned string discriminates real relative-build vs existence
coincidence). Proves namespace-relative semantics only — NOT
responsibility/policy readiness. Stage-1: direct responsibility-SPI
query on WS pid vs direct-job pid — feasible only if the SPI resolves
on iOS (D052 reported symbols absent → missing if so).

## Both-gates helper — prerequisites

Real owned iOS binary at a real path under `/var/mnt/rootfs` (no
alias) + recorded trust admission + direct `ProgramArguments` job +
one bounded send — deferred pending the §4 probes and SPI
reachability. Predicted: registered pid + relative path → populated
reply or next named producer.

No device/TCC/trust/Git actions; D056 corrections retained as history;
D057 backend work untouched.

(handover to codex now)
