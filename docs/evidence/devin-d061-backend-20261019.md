# D061 — Read-only responsibility metadata sweep + installed launcher verification

Date: 2026-10-19. Backend. Device reads only — **no TCC requests, no responsibility
setter, no helper deployment, no job/desktop restart, no source/build/Git mutation.**

Scope per `coordination/_relay/devin-061-backend-task.md`:
1. Read-only inventory of current WS, chroot tccd, native control (PID / lstart /
   path / parent / job / responsibility metadata via existing supported tools).
   Missing or denied → recorded as `MISSING`, never converted into a conclusion.
2. Pull and verify the actually installed `launchdchrootexec` (SHA/UUID + actual
   `posix_spawnattr_setflags` argument).
3. Explicitly separate launchd/job metadata from the libquarantine `0xb6`
   provider query — a responsibility PID field does not prove `0xb6` success.

## A. Installed launcher identity — verified on-device

`/var/jb/usr/macOS/bin/launchdchrootexec` pulled read-only to
`/tmp/d061_launchdchrootexec_device`:

| Field | Value | Class |
|---|---|---|
| SHA-256 | `d36dad4562dd567a86021ef13ce9a0e1deae06ac3d94c99dde52d4016da0437c` | FACT |
| Mach-O UUID | `28F48895-A745-3D55-94A5-EEE1D8DFC138` | FACT |
| Flags site | `mov w1,#0x40` at fileoff `0x43b0` — unconditional `POSIX_SPAWN_SETEXEC` | FACT (capstone disasm) |
| Suspend branch | `mov w1,#0xc0` at fileoff `0x43e0` — reachable only under the `MACWS_SUSPEND_AT_EXEC` env gate | FACT |

Hash differs from all three preserved artifacts (`eecf7c3c`, `1004c790`,
`438c6482`) — re-sign/rebuild linkage differs, but the load-bearing property
is confirmed directly: **the installed binary applies `0x40` (SETEXEC)
unconditionally; `0xc0` exists only on the env-gated suspend path.**
The earlier D056 "orphaned child" model remains retracted; D057's source+staged
verification now extends to the running binary. `__text` identity vs preserved
artifacts was not computed (hash differed; flag semantics verified in place).

## B. Process inventory (same generation as D059/D060)

| PID | Unique PID | PPID | Job | Program | Resolved program path |
|---|---|---|---|---|---|
| 10447 | 109954 | 1 | `UIKitApplication:com.macwsguide.windowserver` | `launchdchrootexec` | `/private/var/mnt/rootfs/…/SkyLight.framework/Versions/A/Resources/WindowServer` |
| 10300 | 109807 | 1 | `com.macwsguide.tccd-system` | `launchdchrootexec` | `/private/var/mnt/rootfs/…/TCC.framework/Support/tccd` |
| 873 | 873 | 1 | native control (`/usr/sbin/mediaserverd`) | `mediaserverd` | `/usr/sbin/mediaserverd` |
| 883 | — | 1 | native iOS `tccd` control | — | `/System/…/TCC.framework/Support/tccd` |

FACT: both chroot processes are direct launchd-tracked jobs (PPID 1, launchd
`unique pid`, `program path` recorded as the **global** `/private/var/mnt/rootfs/…`
spelling). Combined with installed-SETEXEC (§A), the job PID *is* the macOS
process — the same launchd registration relationship the D052 native sender had.

FACT (boundary): launchd job metadata is **not** the libquarantine `0xb6`
provider result. Registration existing does not prove `responsibility_get…()`
would succeed — the provider could still return ENOPOLICY.

## C. Responsibility metadata sweep — all surfaces exhausted, all MISSING

Every existing supported read-only interface was checked for a
`responsible*`/audit field on WS 10447, chroot tccd 10300, and control 873:

| Interface | Result | Class |
|---|---|---|
| `launchctl procinfo <pid>` | 0 `respons*` hits; no auid/asid/audit fields (all 3 pids) | FACT |
| `launchctl print user/501/<job>` | 0 `respons\|audit\|token\|persona` matches | FACT |
| `ps -o resppid/resspid` | `keyword not found` | FACT |
| `/usr/bin/taskinfo <pid>` | unique ID + coalitions + policy only; no responsibility/audit fields | FACT |
| `sysctl kern.proc.pid.*` / `sysctl -a` | no responsible fields | FACT |
| `lsappinfo`, `procstat`, `jtool2`, `audit`, `auditctl`, `auditreduce`, `bsmrecord` | not installed | FACT |
| `responsibility_*` client SPI | SYMBOL_MISSING on iOS (D052-established) | FACT |

**MISSING: `p_responsible_pid` / the `0xb6` provider result for every process.**
No installed read-only tool exposes the kernel responsibility mapping. The
D060-proposed discriminator (registration-level absence vs provider-level
query failure) is **undecidable from the current read-only surface** — this is
a tooling gap, not evidence of either outcome.

## D. What the sweep does establish

- FACT: WS and chroot tccd hold the launchd-side prerequisites (tracked PID,
  session-leader flag, `unique pid` = pidversion-equivalent) that a
  responsibility entry would key on.
- FACT: chroot tccd's job plist carries
  `com.apple.private.tcc.manager.check-by-audit-token` (matches the WS job).
- FACT: the only `#103` witness remains probe pid 46525 (D052); **no
  WS-originated `#103` or `#2` line has ever been observed** for pid 10447.
- THEORY (bounded): if tccd's `0xb6` query keys on launchd's responsibility
  table and the entry follows job registration, WS should have an entry —
  but nothing read-only can confirm the entry exists or that the provider
  accepts it. Both outcomes remain open.

## E. Next discriminating experiment (proposed, not executed)

**Step 1 — local-only, zero device:** enumerate responsibility-related exports
in the extracted iOS shared cache (`libsystem_kernel`/`libquarantine`:
`proc_get_responsible_pid`, `responsibility_get_pid_responsible_for_pid`,
`audit_token_to_*`). Determines whether a userspace read-only channel exists.

**Step 2 — device, needs authorization:** if an SPI exists, a one-shot
read-only helper reads `p_responsible_pid` for 10447 / 10300 / 883 / 873.
Predicted discriminators:

- Entry present for all + WS still #103 later → provider-level rejection
  (ENOPOLICY despite entry) → look at provider's eligibility checks.
- Entry absent for chroot jobs but present for native 883/873 →
  registration-level gap → look at launchd's responsible-pid assignment for
  `POSIX_SPAWN_SETEXEC`/`launchdchrootexec` jobs.
- SPI absent → MISSING is structural; the only remaining witness is tccd's
  gated os_log on a future authorized request.

Missing input today: an authorized deploy of a read-only helper (blocked by
this packet's "no new helper" rule) or a stock tool exposing the field.

## F. Hygiene

- Desktop preserved: `state=ready producer=10447 sequence=4898 reason=validated-final-composite` (4494→4512→4898 advancing across rounds); same process generation.
- All device operations were `cat`/`ps`/`launchctl`/`taskinfo`/`sysctl`/`which` reads plus one `scp` pull of the installed launcher. No writes, no exec of new code, no TCC traffic.
- Pulled binary retained locally at `/tmp/d061_launchdchrootexec_device` for audit.
