# D062 — native responsibility-query interface + error-source audit

Date 2026-10-19, dispatch text carried in user message (relay file
`devin-062-frontend-task.md` not yet written — requirement text taken
verbatim: "本地核验原生责任查询接口/错误来源及独立CFG审核，不TCC、不
setter、不部署不重启"). **Local read-only only.**

Inputs: `analysis/ios163cache/dyld_shared_cache_arm64e` (iOS 16.3
image-name table, 2609 entries) + `.19` subcache;
`analysis/kc_raw_16.3_T8112.bin` (iOS 16.3 kernelcache, IDA-processed);
`analysis/xnu-xnu-8792.81.2`; `analysis/d050-tccd/tccd_arm64e`
(`039f64b7`/9845D17F); `analysis/r005-responsibility-inputs/
libquarantine.dylib` (exact 22F82 `A2D17FF6`).

## 1. Native interface inventory (FACT)

- **libquarantine.dylib: ABSENT from iOS 16.3** — 0 matches in the
  2609-image cache table; `libsystem_kernel`/`libsystem_sandbox`/
  `libsandbox`/`libbsm` all present. The `responsibility_*` SPI family
  has no userspace provider on iOS — D052's SYMBOL_MISSING is
  structural, not an export quirk.
- `__mac_syscall` = **syscall #381 `AUE_MAC_SYSCALL`**,
  `syscalls.master:563` marked `ALL` — the kernel entry exists on iOS.
  Whether iOS `libsystem_kernel` exports the `__sandbox_ms`/
  `__mac_syscall` stub: **THEORY** — `.19` subcache doesn't contain
  libsystem_kernel content (AccessibilityBundles-heavy); unverifiable
  locally. Recorded as missing input, not absence.
- `p_responsible_pid` (`proc_internal.h:359`): KPI-write only
  (`proc_set_responsible_pid` `kern_proc.c:4703`, Private.exports;
  zero in-tree callers → writer is a kext/MAC client); fork-inherited
  (`kern_fork.c:1096`); consumed by `mpo_proc_check_launch_constraints`
  and socket/necp; crashinfo-exported. **No proc_info flavor, no
  syscall, no sysctl reads it** — consistent with backend D061's
  all-MISSING device sweep.
- `launchctl procinfo` responsible fields = launchd bookkeeping —
  **different store** than the `0xb6` provider.

**D064 correction (preserved):** this section's original
"FACT-with-boundary" framing overstated the closure. The image's img4
manifest is CHIP=0x8103 and contains T8103 drivers (D063), but the
`.bin`↔img4 payload linkage was never byte-verified and the live
kernel's policy list was never enumerated — per coordinator review,
provider absence remains **HYPOTHESIS**, not unconditional fact. The
`#103`-by-default reasoning is valid *conditional on* the image being
the running kernel's policy set. See devin-d064 for the corrected
status and what would close it.

## 2. Provider existence — kernelcache evidence (HYPOTHESIS)

- iOS 16.3 T8112 kernelcache: exact MAC-policy name `"Quarantine"` =
  **0 hits** (grep -x). Control names present: `AMFI`,
  `AppleMobileFileIntegrity`, `Sandbox`, `FSandbox`, `CAMFI`.
  Responsibility-implementation log strings: absent.
- Boundary: proves no *statically-registered* "Quarantine" policy in
  this kernelcache image. Runtime kext registration would need the
  kext to ship — `Quarantine.kext` is macOS-only. Live-kernel policy
  enumeration and byte-level `.bin`↔img4 linkage unverified → the
  conclusion is kernelcache-evidenced hypothesis, not device FACT.
- `p_responsible_pid` itself IS populated on iOS — AMFI launch-
  constraints strings reference "responsible process" — the field
  exists; nothing answers `__mac_syscall("Quarantine",…)` about it.

## 3. Error-source map for `#103` (FACT, xnu `mac_base.c`)

`__mac_syscall` (syscall 381): `copyinstr` name →
`mac_proc_check_mac_syscall` precheck (policy `mpo_proc_check_syscall_
mac` vetoes — any errno) → `error = ENOPOLICY` **default** →
`strcmp(mpc_name,"Quarantine")==0 && mpo_policy_syscall` scan →
dispatch or return #103.

So `#103` has exactly these producers: (1) precheck veto returning
ENOPOLICY, (2) **no "Quarantine" policy registered** — the iOS case
per §2, (3) matched handler itself returning ENOPOLICY. On this
kernel the discriminator between (2) and (3) is already answered
by the missing policy name: it's (2) — **provider-level, kernel-build
absence**. No launchd registration can produce a match.

## 4. Independent CFG check — reply producers (verified on binary)

| Site | Shape | Output |
|---|---|---|
| builder bail `0x503f4` | release, `x21=0`, return nil → caller `0x684c cbz → 0x69ac` | ERROR log + **unconditional empty `x23`** send |
| `0x6710` leg (flag=0 + service-prop w28≠0) | log thunk `0x7934` → `xpc_connection_send_message` | early-bail send — distinct producer |
| `0x69e8` leg (flag=0 + w28=0) | `w24=0; b 0x6ba0` | **direct dispatch** — verified |
| `0x6ba0` | function strcmp dispatch (e.g. `TCCAccessCopyInformationForBundle` at `0x69f0`) | handler reply |

`count=0` empty reply still needs the ERROR-line correlate to name the
producer — unchanged from D060.

## 5. Consequence for every chroot sender (CONDITIONAL chain — D064-corrected)

`tccd system` ⇒ isSystemServer=1 ⇒ every `TCCAccessRequest`/Preflight
enters attribution ⇒ `_evaluateResponsibleProcess` ⇒ SPI ⇒
`__mac_syscall("Quarantine",0xb6)` ⇒ **#103** — **conditional on the
Quarantine-policy absence** (§2, hypothesis until live-kernel policy
list or byte-exact cache linkage is verified). If the hypothesis holds,
the R002 probe's #103 was provider absence, not a registration failure,
and no launchd-side registration or job shape changes it.

Legs that never reach the getter (in-code, not suppression):
- non-system tccd instance (flag=0) — not our deployment.
- **Indirect handler:** `0x1238c` builds the requester chain with
  `evaluateResponsibility = !service.macos_shouldAllowSameTeam` —
  for a same-team-flagged service the requester attribution skips
  the getter entirely; subject then goes through
  `initWithAttributionChain:`/`evaluateOverridePolicy…` plus the
  `com.apple.private.tcc.override-prompt-policy` entitlement gate
  (`0x13494`). Whether SkyLight's relevant services carry
  `shouldAllowSameTeam` = unchecked service-registry fact.

## 6. What canNOT split registration vs provider — resolved

Every proposed stock read is now provably insufficient on iOS:
- procinfo/taskinfo/ps/sysctl: no field exists (backend device sweep
  + this source audit agree);
- SPI on iOS: provider binary absent → can't even be issued by a
  native helper without raw `__mac_syscall`;
- #103 identity: always provider-level here — registration-vs-provider
  is a **dead split** on iPadOS; only macOS-kernel or policy-present
  environments could exhibit it.

## 7. Missing inputs + next discriminator

- iOS `libsystem_kernel` export table (`__sandbox_ms`/`__mac_syscall`
  presence) — needs device `dyld_info -exports` or fuller cache slice.
- Optional device probe: `__mac_syscall("Quarantine",0xb6,…)` vs
  `("Sandbox",·)` from an owned helper — #103-vs-other would close the
  last residual on policy absence. Zero TCC; one syscall each.
- Whether any relevant TCC service sets `shouldAllowSameTeam` — needs
  the service registry table (tccd data/TCCDService property map), a
  local RE task on the same binary.
- The entitlement `com.apple.private.tcc.override-prompt-policy` on a
  future broker process — would need a legitimately-entitled platform
  binary, not a forged claim.

(handover to codex now)
