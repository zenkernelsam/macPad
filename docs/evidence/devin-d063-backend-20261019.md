# D063 — Image provenance, libquarantine reconciliation, #103 path correction

Date: 2026-10-19. Backend. Local verification + read-only device identity reads
(`uname`, `ls`, state file). No TCC, no setter, no deploy, no restart, no
check-bypass.

Context: coordinator flagged that `T8112` cannot be evidence for the active
T8103 device, and asked to verify image provenance, the libquarantine
contradiction, and the #103 path. Result: the image is in fact M1-personalized
(filename mislabeled), but two of my D062 evidence rows were scanned on the
wrong-OS image and are corrected here.

## 1. Kernelcache image provenance — FACT

`analysis/kernelcache_16.3_T8112.img4` (21.8 MB IMG4, IM4P tag `krnl`):

| Manifest field | Value | Meaning |
|---|---|---|
| CHIP | `0x8103` | **T8103 = M1** |
| BORD | `0x1e` | iPad Pro-class board |
| Manifest key | `T8103-TssLive-ManifestKey-RevA-DataCenter1` | personalized for T8103 |
| IM4P desc | `KernelManagement_host-331.60.3` | 16.3-train KernelManagement |

`kc_raw_16.3_T8112.bin` (the 80 MB payload I scanned): driver strings for
**both** SoCs — `T8103`×258, `T8112`×25, plus `T8101`/`T819` variants — a
shared multi-SoC iPadOS kernelcache personalized per-device at restore. The
"T8112" filename is a mislabel; the image is an M1-family kernelcache.

Live device identity (read-only `uname`, `sysctl` absent as `/usr/sbin` path —
`uname` only):

- `Darwin Kernel Version 22.3.0: Wed Jan 4 21:25:19 PST 2023;
  root:xnu-8792.82.2~1/RELEASE_ARM64_T8103 iPad13,11` — running kernel is
  **xnu-8792.82.2 on T8103 (M1)**. Device model is iPad13,11 (12.9″ M1 Pro;
  AGENTS.md's "iPad13,6" is the 11″ sibling — same SoC, same matrix family).
- Image build timestamp (AppleImage4 string `Wed Jan 4 21:19:53 PST 2023`)
  is the same build window as the running kernel (`21:25:19`).

→ The D062 "Quarantine"=0 scan was performed on the correct SoC's image and
the same build train (8792.8x) as the live kernel. Local xnu source is
8792.81.2 vs running 8792.82.2 — adjacent-build caveat stands for source
claims, not for the image scan itself.

**On runtime-policy completeness:** `/System/Library/Extensions/` does not
exist on this device (verified) — iOS loads no post-boot kexts; the registered
MAC-policy set at runtime is bounded by the kernelcache's static registrations.
The image scan therefore *is* the runtime policy list, minus any hypothetical
kernel patch (Dopamine registers none).

## 2. The libquarantine contradiction — resolved, D062 rows retracted

R009 (`r009-exact-libquarantine-boundary-20261018.md:129-131`) already
attributed `analysis/device-leftovers-20261006/.../libquarantine.dylib`
(UUID `8CE8DAD0`, Quarantine-181.120.2) as a **macOS 15.6** image — a leftover
from the mac15-era device dumps (`var-mobile/dscq/` = old extraction output).
The `libsystem_kernel.dylib`/`libSystem.B.dylib` pulls in the same directory
share that provenance.

**RETRACTION (D062 §1):** the three rows claiming "iOS libquarantine /
libsystem_kernel / libSystem.B contain zero responsibility strings" were
scanned on **macOS 15.6** images — they say nothing about iOS. Apologies for
the wrong-OS scan; the corrected iOS evidence is below.

**iOS-side libquarantine status (FACT, device-verified this round):**

- `ls /usr/lib/system/libquarantine.dylib` → **No such file or directory** —
  no on-disk copy (iOS system dylibs are cache-only).
- D062-frontend: iOS 16.3 cache image table (2609 entries) contains **no
  libquarantine image** — no userspace provider of `responsibility_*` SPI
  exists on iOS at all.
- Native iOS tccd (UUID 0120CE08): zero responsibility imports (R005,
  exact-image). Live dlsym → SYMBOL_MISSING (D052). All three agree:
  `responsibility_*` is structurally absent on iPadOS userspace.

## 3. #103 path — corrected producer model

Per `__mac_syscall` (`mac_base.c:1567-1620`), ENOPOLICY(103) has **three**
possible producers (R009 §4 — my D062 "means precisely no policy" was
over-narrow, retracted):

1. `mac_proc_check_mac_syscall` precheck returns ENOPOLICY;
2. dispatch preset `error = ENOPOLICY` survives — no registered policy named
   "Quarantine" with `mpo_policy_syscall`;
3. a matched handler itself returns ENOPOLICY.

**Which arm applies on this device:** the kernelcache has zero `"Quarantine"`
policy-name strings → (3) is impossible (no such policy can register) and (2)
is forced *whenever the call reaches dispatch*. The residual formal
possibility is (1) — a precheck denial — which would be a different producing
arm, same errno. A Quarantine-named `mpo_policy_syscall` handler provably
cannot exist on this kernel.

**Stub-name correction:** R009 shows the 22F82 getter's stub GOT slot
(`0x1dfef8f88`) lies outside all extracted segments — the provider name is
**not resolvable from the slice**. `__mac_syscall` (= `__sandbox_ms`, same
address in the 15.6 image) is a version-adjacent THEORY, not slice FACT. What
remains slice-FACT: policy string `"Quarantine"` at `0x18b321e0b`, selector
`w1=0xb6`, and the pid/pidversion syscall-arg packing — matching the
`__mac_syscall` ABI exactly.

## 4. Net status after correction

- FACT: on this device's kernel, `__mac_syscall("Quarantine",·)` cannot match
  a registered policy → ENOPOLICY whenever dispatched; `responsibility_*` has
  no userspace provider either (no libquarantine image on iOS at all).
- FACT: `tccd system` (Ventura) hardwires sender attribution evaluate=YES →
  reaches the getter → `#103` for every sender. Registration cannot fix it.
- THEORY (formal gap): precheck-vs-no-match arm unseparated — both produce 103;
  distinguishing needs a runtime boundary observation (R009's proposed
  read-only trace / a `__mac_syscall` A/B helper — requires authorization,
  zero TCC either way).

## 5. Hygiene

Desktop untouched: `state=ready producer=10447 sequence=5145
reason=validated-final-composite` (4494→4512→4898→5145 across rounds).
Device ops: `uname`, `ls`, `cat` — reads only.

(handover to codex now)
