# D092 — what the D091 health evidence closes vs leaves open

Offline synthesis only. Inputs: D070 checkpoint, D091 authorized
health check, D088+D090 authorization packet (rev-4 + errata).

## What D091 closes

- **Generation continuity (checkpoint → now).** WS pid 10447 and both
  tccd pids (883, 10300) with identical lstarts: the generation that
  produced every D070+ fact is the generation a future v3 run would
  observe. No restart, no panic, no re-registration epoch — a snapshot
  taken now reads the *same* `mac_policy_list` epoch as the one the
  entire attribution chain describes. Stale-epoch risk eliminated.
- **Desktop is producing, not parked.** `sequence 6055 → 11041` under
  `state=ready`/`validated-final-composite`: continuous validated
  composites across days. The "uptime ≠ pixels" rule is satisfied by
  the advancing counter; any future measurement starts from a healthy
  baseline, and post-witness `delta>0` is the expected healthy
  signature (delta=0 stays the anomaly per the packet).
- **Device identity pins the packet target.** `uname` = 22.3.0,
  `iPad13,11` (T8103); rootfs `SystemVersion` = macOS **13.4.1/22F82**
  — the analyzed image. The v3 runner's `version[]` expected string
  (`…RELEASE_ARM64_T8103`) and `staticBase` are consistent with the
  live endpoint, so the identity gates should pass rather than
  STOP — the packet's preconditions are met on the live device.
- **SSH health on the authorized path.** Port-22 password auth via
  `sshpass -e` works (two clean exits); the packet's credential
  mechanics are operational on the authorized principal.

## What remains open (unchanged)

- **The Quarantine-policy question** — still the one fact only the
  bounded snapshot can supply. D091 read no kernel memory; nothing
  about `mac_policy_list` was or could be observed by ps/cat.
- **Responsibility/TCC runtime legs** — the kernel-side registration
  arm (open → the snapshot), the precheck-veto arm (open, needs the
  snapshot's ops flags), and the userspace TCC legs (frontend packet,
  separate). The D065 routing contract and the D050/D055/D060
  attribution chain stand; none is closed by a health check.
- **F1 route firing + Host pixel witnesses** — the two current-
  generation runtime witnesses the larger project still lacks
  (D072). Orthogonal to both D091 and v3.
- **Physrw arm under procursus python** — still the single
  unverifiable-offline link inside the v3 packet; its fail-closed
  `physrw-unarmed`/`missing-authorization` terminal stands.

## Why the named v3 grant is still separate

D091's grant covered `ps`, `cat`, `uname`, `mount`, `grep` — a health
read only. It does not extend to `libjailbreak` primitives, kernel
memory reads, or python3-via-stdin execution. The v3 packet is pinned
to sha256 `8bb90399…` under rev-4 + errata text and requires the
user's **named** authorization of `macws-policy-snapshot-v3` — a
different mechanism, different read set, different risk class. D091
reduces the packet's *contextual* uncertainty (healthy same-epoch
target) but grants nothing toward its execution.

## Status

All v3 preconditions visible offline or from D091 are satisfied:
device identity, generation stability, desktop health, SSH mechanics.
The only remaining inputs are the named grant itself and the runtime
physrw-arm check inside the run.
