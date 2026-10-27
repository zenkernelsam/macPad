# D091 — user-authorized read-only SSH health check

Scope executed: single authorized endpoint `root@<authorized-device>`
on TCP 22, `sshpass -e` with `SSHPASS` in a transient env var only —
never argv, files, logs, or Git (unset after each call). No writes,
deploys, restarts, trustcache, KRW/TCC, or desktop alteration. Two
read-only ssh invocations; both exited 0.

## Collected (raw values)

| Item | Value |
|---|---|
| SSH exit status | `0` (both invocations) |
| WindowServer | pid **10447**, lstart **Mon Oct 19 00:17:23 2026** |
| tccd (system) | pid **883**, lstart **Sun Oct 18 12:26:48 2026** |
| tccd (user/session) | pid **10300**, lstart **Mon Oct 19 00:17:17 2026** |
| `macws_final_composite.state` | `state=ready`, `producer=10447`, `sequence=11041`, `updated=1792376737.323332`, `reason=validated-final-composite` |
| Rootfs identity | `SystemVersion.plist`: ProductName **macOS**, ProductVersion **13.4.1**, ProductBuildVersion **22F82** |
| Device/kernel | `uname -r` = **22.3.0** (xnu-8792.82.x), `uname -m` = **iPad13,11** (T8103); `/private/var/mnt/rootfs` holds `/var/jb/usr` bindfs + devfs — chroot mount live |

## Read vs checkpoint

- **Same desktop generation** as the D070 checkpoint: WS 10447 and
  both tccd pids/lstarts identical — no restart, no panic.
- **Sequence advanced** 6055 → **11041** with `state=ready` and
  `reason=validated-final-composite` — the compositor is not merely
  alive; it is continuously producing validated final composites.
  This is the "advancing sequence" witness, not uptime.
- **Rootfs identity confirmed**: the mounted macOS rootfs is
  **Ventura 13.4.1 / 22F82** — the analyzed image, matching the
  SkyLight UUID target `96676A53-…` and the policy-snapshot packet's
  kernel identity (`22.3.0 … RELEASE_ARM64_T8103`).

## Redaction / hygiene

- Password used only via `SSHPASS` env + `sshpass -e`; unset after
  each command; never in argv, output, this file, or Git.
- Host detail: authorized endpoint only; no additional host inventory
  collected.

(handover to codex now)
