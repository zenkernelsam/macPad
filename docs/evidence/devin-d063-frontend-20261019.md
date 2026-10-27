# D063 — kernelcache provenance + libquarantine contradiction + #103 path audit

Date 2026-10-19, packet `coordination/_relay/devin-063-frontend-task.md`
(file not yet in relay — dispatch text executed verbatim: "T8112不能当
现役T8103证据，先核实镜像来源、libquarantine矛盾和103路径；不绕过
检查、不TCC、不重启"). **Local read-only.**

## 1. Image provenance — the filename is wrong; content IS T8103-class

| Check | Result | Class |
|---|---|---|
| Device (evidence captures) | `iPad13,11`, `xnu-8792.82.2~1/RELEASE_ARM64_T8103` | FACT |
| `kernelcache_16.3_T8112.img4` manifest | `IM4M` entries ×2, both `CHIP INTEGER = 0x8103` | FACT |
| `kc_raw_16.3_T8112.bin` SoC strings | `AppleLEAPController_T8103`, `AppleMCA2_T8103`, `AppleMCA2Cluster_T8103` AND `AppleT8112DPTXPort`/`AppleT8112USBXHCI`/`AppleT8112DisplayCrossbar` — shared arm64e cache covering both | FACT |

**Resolution:** the image's own manifest tags it CHIP=0x8103 — a
T8103-family (M1, the deployed iPad13,11) kernelcache, shared arm64e
build that also carries T8112 drivers. The "T8112" filename is a
mislabel. The MAC-policy-name evidence (D062) therefore **applies to
the running device's kernel family** — the "Quarantine" string is
absent across all 80 MB covering every listed kext. Residual: the
`.bin`↔`img4` payload linkage is asserted via manifest+driver
co-membership, not a byte-level decompress-and-compare; a T8103-only
cache pull from the device would close it fully.

**D064 correction (preserved):** per coordinator review, the residual
above means the provider-absence conclusion is **HYPOTHESIS**, not
device FACT — `CHIP=0x8103` does not close byte-level payload/runtime
linkage, and the live kernel's registered-policy list was never
enumerated. All downstream "#103 is unconditional" statements in this
doc are conditional on this hypothesis.

## 2. libquarantine contradiction — dissected

File `analysis/device-leftovers-20261006/var-mobile/dscq/usr/lib/system/
libquarantine.dylib`, UUID `8CE8DAD0-52D6-3F46-98EF-161E1397FB48`:

- `vtool`/`LC_BUILD_VERSION`: **platform MACOS minos 15.6 + platform
  MACCATALYST minos 18.6** — a macOS/Catalyst universal build
  (Quarantine-181.120.2). R008's platform call was correct.
- `nm -gU` symtab: **35+ responsibility exports** including
  `_responsibility_get_responsible_audit_token_for_audit_token`
  `0x18da94750`, the `responsibility_set_*` family,
  `responsibility_spawnattrs_*`, `responsibility_init` — plus internal
  `__qtn_syscall_responsibility_*` wrapper names at stroff `15408`.
  R008's `0xb6` getter disasm was on this very file — the API is real.

**Backend D062's claim "iOS libquarantine (UUID 8CE8DAD0) contains
zero responsibility code/strings" is contradicted by the file's own
symbol table.** Likely measurement error: a `strings` pass counts
literal C-strings; symbol names live in `LC_SYMTAB` stroff (raw bytes
do contain them — offset `17799` shows
`responsibility_get_responsible_audit_token`). If the backend pulled a
*different* file under the same UUID claim, the collision needs
explanation — same UUID ⇒ same content. And "the only reachable
libquarantine file on-device is this quarantined macOS copy under
`var-mobile/dscq`" remains consistent with: **iOS's own dyld cache
lists zero libquarantine images** (D062-frontend, 2609-entry table) —
the SPI is absent from iOS userspace regardless.

**Corrected net:** (a) iOS system libquarantine — absent (cache table
FACT); (b) macOS libquarantine — present, full API, `__mac_syscall`
dispatch — but its syscall only works where the kernel registers the
"Quarantine" policy, which this kernel **hypothetically** doesn't
(image-evidenced; runtime linkage unverified — §1 D064 correction).

## 3. #103 path — re-verified, narrowed to one producer on iOS

`__mac_syscall` (syscall #381, `ALL` platforms — iOS kernel has the
entry; userspace stub `__sandbox_ms`/`__mac_syscall` presence in iOS
`libsystem_kernel` = THEORY, `.19` subcache lacks it):

```
copyinstr(policy name)          → EFAULT-class only
mac_proc_check_mac_syscall      → mpo_proc_check_syscall_mac vetoes
                                  (any errno — not necessarily 103)
error = ENOPOLICY               → mac_base.c:1587 default
strcmp(mpc_name,"Quarantine")==0 && mpo_policy_syscall → dispatch
no match                        → return ENOPOLICY 103
```

R008's "three 103 sources" narrows on this kernel: no "Quarantine"
policy is registered (§1 boundary) → **the producer is the
no-match default at `mac_base.c:1587`**. A precheck veto returning
ENOPOLICY is only possible if a registered policy implements
`mpo_proc_check_syscall_mac` and vetoes the name — no evidence either
way; AMFI/Sandbox presence is confirmed but their hook behavior is
unaudited. This is the residual THEORY, marked.

**If** the provider is absent (hypothesis, §1 correction), **every**
`responsibility_*` selector — getters AND setters (`0xb4`–`0xbb`) —
returns #103 on this kernel. A setter call would prove nothing new and
is off-limits anyway. If the hypothesis fails (a Quarantine policy
registered at runtime), the selector set is live and #103 instead
indicates a policy-internal denial — distinguishable only by observing
the selector's errno on a known-attributed native sender.

## 4. What still can't distinguish, and what's now decidable

- `p_responsible_pid` remains KPI-written only (no userspace flavor) —
  stock tools can't read it (backend sweep consistent).
- Registration-vs-provider split is settled **conditionally**: IF
  provider absent ⇒ #103 is unconditional and the earlier "procinfo
  present ⇒ provider live" split was never valid (different store).
  If provider present ⇒ #103 has policy-internal provenance and
  registration state is back on the table.
- Remaining genuinely-unknown: whether `tccd system`'s other legs
  (Indirect requester-chain `evaluateResponsibility=!shouldAllowSame
  Team`; `isSystemServer=0` dispatch) produce non-empty replies for
  real ops — a tccd-local service-table question.

## 5. Missing inputs

- iOS `libsystem_kernel` export scan for `__sandbox_ms`/`__mac_syscall`
  (device `dyld_info` or fuller cache subcache — `.19` lacks it).
- Byte-level `.bin`↔img4 payload match (or device pull of
  `/System/Library/Caches/com.apple.kernelcaches/kernelcache`) to make
  SoC closure airtight.
- Whether any registered iOS policy's `mpo_proc_check_syscall_mac`
  vetoes "Quarantine" (kernel hook audit on the same .bin).
- Service-registry map: which `TCCDService`s set
  `macos_shouldAllowSameTeam`/`macos_isPerSystem` (local RE only).

Boundaries kept: no device, no TCC, no setter, no deploy, no restart.
(handover to codex now)
