# D063 backend result — provenance verified, two D062 rows retracted

2026-10-19. Local + read-only device identity checks only. No TCC, no setter,
no deploy, no restart, no bypass. Evidence: `docs/evidence/devin-d063-backend-20261019.md`.

## 1. Kernelcache provenance — it IS an M1 image; filename mislabeled

`kernelcache_16.3_T8112.img4` manifest: **CHIP=0x8103 (T8103/M1)**,
BORD=0x1e, `T8103-TssLive-ManifestKey` — personalized for M1. The payload
`kc_raw` carries both SoC's drivers (T8103×258/T8112×25) — a shared
multi-SoC iPadOS cache. Live device: `xnu-8792.82.2~1/RELEASE_ARM64_T8103`,
**iPad13,11** (M1 12.9″ — AGENTS.md "iPad13,6" is the sibling 11″; same SoC).
Build timestamps match (Jan 4 2023, ~20min apart). The D062 "Quarantine"=0
scan therefore stands as evidence for the *active* SoC's kernel; residual
caveat is only the 8792.81.2-vs-8792.82.2 source-adjacency for xnu claims.
`/System/Library/Extensions/` absent → no post-boot kexts → image scan =
runtime policy set.

## 2. libquarantine contradiction — resolved by retraction

R009 already documented the `8CE8DAD0` dscq pull as **macOS 15.6**, not iOS —
my D062 scanned the wrong-OS image for all three "iOS pull" rows
(libquarantine/libsystem_kernel/libSystem.B). **Retracted.** Corrected iOS
status (all FACT): `/usr/lib/system/libquarantine.dylib` absent on device fs
(verified this round), absent from the 2609-entry iOS cache image table
(frontend), native tccd zero imports (R005), live dlsym MISSING (D052) —
iOS has **no** libquarantine/responsibility provider at all.

## 3. #103 path — three producers, one eliminated structurally

`__mac_syscall` produces 103 from: precheck deny / no-match preset / handler.
Zero "Quarantine" strings in the kernelcache eliminates the handler arm and
forces the no-match arm whenever dispatch is reached — the only formal gap
left is precheck-vs-no-match (same errno, different producing arm; needs the
R009 read-only trace or a `__mac_syscall` A/B helper — authorization-gated,
zero TCC). Stub-name correction: `__mac_syscall`/`__sandbox_ms` provider is
15.6-adjacent THEORY for 22F82 (GOT slot in missing segment); slice-FACT is
the `"Quarantine"` string + `0xb6` + arg packing only.

## 4. Net

- Every sender chain on `tccd system` reaches `evaluate=YES` → getter →
  #103 — provider absent at kernel-build level on the *actual* T8103 device.
- No registration/setter can produce non-#103. The `p_responsible_pid` field
  is unreachable from userspace regardless.
- Retractions recorded in-place in the evidence doc; D062 doc's wrong-OS rows
  flagged. Desktop `ready` seq 5145, same generation.

(handover to codex now)
