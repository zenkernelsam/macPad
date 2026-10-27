# D064 — Exact kernel evidence linkage + claim calibration

Date: 2026-10-19. Backend. Local analysis + two small read-only device outputs
(`uname`, `sha256sum` digest of the deployed kernelcache — a 32-byte result,
no file pull). No TCC, no setter, no MAC syscall, no deploy, no restart.

Task: verify what the saved artifacts actually establish; withdraw
unconditional provider-absence/registration-never-works claims; keep history;
produce one bounded discriminator.

## 1. Artifact identity inventory (FACT)

| Artifact | SHA-256 / identity | Provenance |
|---|---|---|
| `kernelcache_16.3_T8112.img4` | `78959dd973ed3584532d51424274e183d843a2add801bd017f37d64e7b62dcc2`, 21.8 MB, IMG4/IM4P `krnl` | manifest: CHIP=`0x8103`, BORD=`0x1e`, `T8103-TssLive-ManifestKey`; desc `KernelManagement_host-331.60.3` — **M1-personalized, filename mislabeled** |
| `kc_raw_16.3_T8112.bin` | `6673ce2889604cbfb7a9dc938000be0c48486874c9aebae0cdd4870c514f2b38`, 80 MB fat Mach-O | embedded version string `0xe91ee5`: `Darwin Kernel Version 22.3.0: Wed Jan  4 21:25:19 PST 2023; root:xnu-8792.82.2~1/RELEASE_ARM64_T8103` |
| Live device kernel | `uname`: **identical string** + `iPad13,11` | xnu-8792.82.2, T8103 |
| Local xnu source | `xnu-xnu-8792.81.2` | adjacent build to running 8792.82.2 — source-drift caveat applies to all source claims |

## 2. Extraction chain — what is and isn't proven

**Closed (FACT):** `sha256sum` of the deployed boot kernelcache on the device
(`/private/preboot/<hash>/System/Library/Caches/com.apple.kernelcaches/
kernelcache`, 21,845,088 B) = **`78959dd9…` = our img4 byte-for-byte**. The
repo img4 *is* the device's deployed kernelcache image, not merely a matching
build.

**Open (MISSING):** `kc_raw_16.3_T8112.bin` ↔ img4 payload byte-linkage is
**not** verified — the IM4P payload is `bvx2`/RAWPack2 and no decompressor
exists locally (img4tool/bvxdec/lzfse/librawpack all absent; DyldExtractor venv
has none). Consistency evidence: identical embedded build string + shared
T8103/T8112 driver set. **Do not claim `kc_raw` is this img4's payload at byte
level** — the Sept-30 doc records it as "actually T8103 xnu-8792.82.2", and the
embedded string matches, but the decompression step was never run.

**Not claimable at all:** the *mapped* kernel's byte identity — deployment-hash
match + build-string match do not exclude runtime patches.

## 3. Claim calibration (withdrawals preserved, not deleted)

- **D062/D063 "provider absent at kernel-build level" → downgraded to:**
  no `"Quarantine"` NUL-string exists in the analyzed image → no policy *in
  this image* can statically register that name. Residual gaps: (a) kc_raw↔img4
  payload linkage unverified per §2 — the scan covers the *artifact*;
  (b) string-scan cannot exclude a dynamically-composed policy name or a
  hypothetical runtime patch — bounded by enumerating `mac_policy_register`
  callers (§5); (c) `/System/Library/Extensions` absent bounds *post-boot
  kext* registration only.
- **"registration can never fix #103" → withdrawn** as stated: correct only
  if provider-absence holds at runtime; the registration-enumeration + a
  runtime boundary observation are the two closing experiments.
- **D062's three "iOS pull" rows remain retracted** (macOS 15.6 images).
- **R009's three-producer model is the correct #103 attribution** — precheck /
  no-match preset / handler. The image evidence eliminates only the *handler*
  arm; precheck-vs-no-match remains formally open.

## 4. `__mac_syscall` wrapper — exact-ABI evidence (source-scoped)

R009-exact on the Ventura libquarantine getter `0x18b320b00`→helper
`0x18b320b2c`: policy string `"Quarantine"` at `0x18b321e0b` (fo `0x2e0b`),
selector `w1=0xb6`, arg struct `{pid@+0, pidversion@+8, out-token-ptr@+0x10,
&zero@+0x18, x3@+0x20}` — matches `__mac_syscall(policy, call, arg)` ABI.
Provider name `__mac_syscall`/`__sandbox_ms` is **15.6-adjacent THEORY** —
the stub's GOT slot `0x1dfef8f88` lies outside all extracted segments.

xnu `__mac_syscall` (`mac_base.c:1567`, source 8792.81.2 — drift caveat):
copyinstr → `mac_proc_check_mac_syscall` MAC veto (any errno) → preset
`ENOPOLICY` → name-strcmp + `mpo_policy_syscall!=NULL` scan → dispatch.
ENOPOLICY=103 (`errno.h:256`). Selectors `0xb4–0xbb` all dispatch through the
same name lookup — equally affected whatever the outcome.

## 5. The legitimate service-handling path (what could reply at all)

Verified in the exact tccd image:

- `tccd system` (`argv[1]=="system"`, `0x6064`→`0x6084` flag → `0x6108` setter):
  `isSystemServer=1` → every connection takes the attributed leg → sender chain
  `evaluate=YES` → `0xb6` → empty `x23` reply whenever the query fails.
- **`com.apple.tccd` (no `system` arg) → `isSystemServer=0`**: the `0x6708`
  leg sends messages straight to dispatch **without attribution** for services
  where `macos_isPerSystem==0` (flag=0 + prop≠0 → the distinct early bail at
  `0x6710`). This is Apple's own user-domain instance design — the only
  in-code path to a populated reply that never touches the `0xb6` provider.
- `macos_shouldAllowSameTeam` skips the responsibility getter **subject-side**
  inside `do_Indirect` (`w2=!flag` at `0x1238c`) — unreachable while sender
  attribution bails first.
- **MISSING input:** the per-service flag map (`isPerSystem`,
  `shouldAllowSameTeam`) — service definitions live in `TCC.framework`
  resources not in the extracted set (tccd binary carries 129 `kTCCService*`
  names but no flag table; ivar flags are `TB` BOOLs set at service init).

## 6. One bounded next discriminator

**Enumerate `mac_policy_register` callers in `kc_raw`** (local-only): locate
the kernel's exported `mac_policy_register` (via its callers around the
`"mac_policy_register failed"` panic strings, or the kernel exports table),
walk each callsite's `mac_policy_conf.mpc_name` pointer → the exhaustive
*registerable* policy-name set for this exact image.

- Proves: whether any in-image code can register `"Quarantine"` — closes the
  string-scan gap at callsite completeness. If absent → provider absence on
  this image is registration-complete.
- Cannot prove: (a) runtime-added policy (jailbreak kernel patch — none known,
  but a runtime list read is the only check and no userspace interface exists);
  (b) a policy named "Quarantine" whose `mpo_policy_syscall` is NULL —
  dispatch would still return the preset ENOPOLICY;
  (c) the precheck arm — needs the R009 runtime boundary trace (breakpoint on
  the `__mac_syscall` dispatch reading which arm produces 103) — requires
  authorized helper/debugger, zero TCC.

Missing inputs for full closure: bvx2 decompressor (img4↔payload byte link) —
*or* accept build-string+deployment-hash consistency and scope conclusions to
"this image"; TCC.framework resources for the service-flag map (device rootfs
has them — a read-only file pull next round).

## 7. Hygiene

Device ops: `uname`, `sha256sum`, `ls`, `cat` — small read-only outputs only.
Desktop last observed `state=ready producer=10447 seq 5145` (D063 read).
No TCC/setter/MAC-syscall/deploy/restart.

(handover to codex now)
