# D061 backend result — read-only responsibility sweep + installed launcher check

2026-10-19. Read-only device checks only: no TCC requests, no responsibility
setter, no helper deployment, no job/desktop restart, no source/build/Git changes.
Evidence: `docs/evidence/devin-d061-backend-20261019.md`.

## Installed launcher — verified (FACT)

`/var/jb/usr/macOS/bin/launchdchrootexec` pulled → SHA-256
`d36dad4562dd567a86021ef13ce9a0e1deae06ac3d94c99dde52d4016da0437c`, UUID
`28F48895-A745-3D55-94A5-EEE1D8DFC138`. Disasm: `mov w1,#0x40`
(`POSIX_SPAWN_SETEXEC`) unconditional at fo `0x43b0`; `w1=#0xc0` only on the
`MACWS_SUSPEND_AT_EXEC` branch. **Installed binary = SETEXEC build** — the
D056 orphan retraction (D057) extends to the running binary. Hash differs from
all preserved artifacts (re-sign/rebuild linkage); flag semantics verified in
place, `__text` identity not computed.

## Process inventory (FACT)

Same generation: WS pid 10447 (unique 109954, job
`UIKitApplication:com.macwsguide.windowserver`), chroot tccd 10300 (unique
109807, job `com.macwsguide.tccd-system`), native controls tccd 883 and
mediaserverd 873 — all PPID 1, `program=launchdchrootexec`, `program path`
recorded globally (`/private/var/mnt/rootfs/…`). Launchd prerequisites for a
responsibility entry exist at the same level as native daemons.

## Responsibility metadata — all MISSING (FACT)

`launchctl procinfo` (all pids), `launchctl print` job dump, `ps resppid`,
`taskinfo`, `sysctl kern.proc.*` — zero `respons*`/audit fields exposed.
`lsappinfo`/`procstat`/`jtool2`/`audit*` — not installed. `responsibility_*`
SPI — SYMBOL_MISSING on iOS (D052). **No existing read-only tool can read
`p_responsible_pid` or the `0xb6` provider result.** The D060 discriminator
(registration-absence vs provider-failure) is undecidable from this surface —
a tooling gap, not evidence for either outcome. Job metadata ≠ `0xb6` success,
per packet warning.

Bounds kept: only recorded `#103` witness remains probe pid 46525 — no
WS-originated `#103`/`#2` has ever been seen for 10447. No fix implied.

## Next discriminator (proposed, not run)

1. **Local-only first:** enumerate `proc_get_responsible_pid` /
   `responsibility_get_pid_responsible_for_pid` / `audit_token_to_*` exports in
   the extracted iOS shared cache — determines if any userspace read-only
   channel exists.
2. **Device (needs authorization):** if an SPI exists, one read-only helper
   reads `p_responsible_pid` for 10447/10300/883/873. Entry-present+later-#103 →
   provider rejection; entry-absent for chroot jobs only → registration gap
   (launchd responsible-pid assignment for SETEXEC launcher jobs); SPI absent →
   structural MISSING, only remaining witness is tccd's gated os_log.

Blocked input: this packet's no-new-helper rule; needs a packet authorizing a
read-only helper (or a stock tool, if the export scan finds one already
shipped).

## Hygiene

Desktop untouched: `state=ready producer=10447 sequence=4898
reason=validated-final-composite`, advancing (4494→4512→4898). All ops were
reads + one scp pull. No writes/exec/TCC/setter. Credentials env-only.

(handover to codex now)
