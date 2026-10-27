# devin-056 backend result — upstream attribution compatibility design

> **CORRECTION (D057):** the launch-contract finding below is retracted —
> `POSIX_SPAWN_SETEXEC` is active (source :119/:133, verified `w1=0x40` in all
> preserved binaries), so the macOS process IS the launchd-tracked pid; there
> is no orphan child and "SETEXEC as the missing fix" was based on a misread.
> The "architecturally unreachable" verdict is also retracted as over-broad.
> Stage-1's ENOPOLICY cause re-opened. See D057 evidence/result files.

2026-10-19 · local-only · packet `coordination/_relay/devin-056-backend-task.md`
Evidence doc: `docs/evidence/devin-d056-upstream-attribution-design-20261019.md`
No device actions, no TCC traffic, no trust/identity writes, no production
source/Git/device mutations. Desktop untouched throughout.

## Corrections absorbed (D055 frontend review)

- **Double-root warning accepted**: a helper placed inside
  `/var/mnt/rootfs` resolves globally to `/var/mnt/rootfs/tmp/X`, and the
  chrooted tccd re-roots it to `/var/mnt/rootfs/var/mnt/rootfs/tmp/X` →
  ENOENT regardless. Rootfs placement is NOT a fix — the proposed
  rootfs-helper resend was not executed and is dropped permanently.
- xnu caller-context `vnode_lookup` re-resolution adopted as the
  stage-2 producer model (existence-only, vnode never compared to
  sender textvp).

## New verified facts this round

1. **Launch contract (repo source)**: `launchdchrootexec` chroots, then
   `posix_spawn`s the macOS binary as a NEW pid, then `return 1` — the
   launchd-tracked process exits; the macOS child is an orphaned stray.
   `CS_LAUNCH_TYPE_SYSTEM_SERVICE` propagates the AMFI launch class
   only. `POSIX_SPAWN_SETEXEC` exists commented out. → structural #103
   cause.
2. **Getter internals (local disasm, libquarantine A2D17FF6 @0x18b320b00)**:
   token val[5]=pid + val[7]=pidversion extracted, dispatcher call
   `w1=0xb6` — pid-identity-keyed provider query. ENOPOLICY = no
   responsibility entry for that pid. Entries follow launchd job
   tracking of the exact pid; not inherited across posix_spawn.
3. **Interposition audit (mac_hooks.m)**: interposes are `mach_msg*`,
   `mach_port_construct`, `mach_vm_map`, `mach_make_memory_entry_64`,
   `vproc_swap_string`, `audit_token_to_asid`, `audit_token_to_auid`,
   `auditon`, `getaudit_addr`. NO `proc_pidpath*`/`responsibility_*`/
   `task_info`/`getpid` interposes — tccd's stage-1/2 calls hit real
   kernel APIs. Recorded honestly: the audit interposes forge
   auid=501/asid in *log* fields (diagnostic-era scaffold; explains
   `sender_auid=501` artifact, unrelated to the responsibility table).
4. **Indirect schema present in tccd**: `TCCAccessRequestIndirect`,
   `indirect_object_token`, `AUTHORIZATION_SUBJECT_*_DICTIONARY_KEY`,
   `responsible*` keys, `responsibility.set-arbitrary` entitlement
   strings — a protocol-native broker path exists; admission gates not
   yet enumerated.

## Design comparison outcome

| Design | Verdict |
|---|---|
| (a) Authentic launchd-owned process via `POSIX_SPAWN_SETEXEC` | Legit fix for stage-1 (tracked pid becomes the macOS process; real pid/pidversion/vnode). Changes job lifecycle contract → per-job audit needed. Still fails stage-2 (double-root). NOT deployed. |
| (b) Native-side broker via `TCCAccessRequestIndirect` | Only remaining avenue for a populated verdict. Needs: broker exec-admitted at a namespace-symmetric canonical path (same bytes in both roots — honest placement, not foreign alias) AND Indirect admission (entitlement/schema unknown). Both unverified. |
| (c) Kernel namespace-relative lookup | Does not exist. Dead option. |

## Structural verdict

**No populated verdict reply is architecturally reachable for
chroot-resident senders (WindowServer included) while tccd stays
chrooted** — stage-2's caller-context re-lookup can never express a
`/var/mnt/rootfs/…`-global path, and unchrooting tccd would break its
TCC store semantics. This is a bound to accept, not a bug to chase.

## Smallest discriminating next experiments (both proposed, neither run)

1. Read-only disasm of the `TCCAccessRequestIndirect` handler
   (`0x6fc0 → 0x100011aa4`) in `analysis/d050-tccd/tccd_22F82` —
   required keys, entitlement gates, and whether it attributes via the
   broker's chain (design viable) or re-derives the subject's chain
   (chroot subjects still fail → (b) dead). Zero device.
2. Device read-only probe: writable iOS paths whose spelling is
   canonical inside the rootfs AND admit owned-binary exec — the
   broker placement constraint.

## Boundaries honored

No TCC requests sent; no trust lists written; no identity setters; no
filesystem aliases; no production deployment changes; no
WindowServer/desktop restart; no guessed private APIs implemented;
no fabricated paths or responsibility. Desktop health last verified
`state=ready`, producer 10447, sequence advancing (D055 close-out).

(handover to codex now)
