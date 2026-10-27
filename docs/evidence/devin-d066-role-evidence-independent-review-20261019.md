# D066 frontend — role logic correction, R004 getter reuse, and A/B safety audit

Local-only review. No device, no TCC requests, no MAC syscalls, no
setters, no source/build/Git changes. Inputs: R004 artifacts
(`coordination/artifacts/R004-responsibility-boundary-20261018.json`,
`R004-responsibility-controls-20261018.txt`), D062–D065 evidence, live
source (`libmachook/MacWSSenderObservation.c`, `mac_hooks.m`).

## 1. Corrections applied (history preserved in-place)

- **D064 "unreachable by construction" → overclaim.** The role contract
  (`isPerSystem=1` → `0x6710` early-bail) excludes only the *non-system
  alternative*. The authentic `com.apple.tccd.system` endpoint exists,
  is correctly routed (D065), and is the legitimate path — blocked only
  *conditionally* on the unresolved provider hypothesis. Global-input
  architectural impossibility is **not** proven. Corrected in
  `devin-d064-service-registry-review-20261019.md` §2/§5.
- Backend D065's parallel claim ("unreachable … by role contract,
  independent of the provider hypothesis") carries the same conflation
  and is backend's to correct — flagged here for cross-check only.
- Same-team Indirect wording already corrected in D064 (subject-side,
  post-attribution).

## 2. What R004 already tested — do not repeat

From `R004-responsibility-controls-20261018.txt` (verbatim evidence):

| Control | What ran | Result |
|---|---|---|
| A | Ventura chroot getter, **chroot self token** | `rc=-1 errno=103 "Policy not found"`, out zeroed |
| B | Ventura chroot getter, **live native iOS token** (pid 51948, ps-verified alive during call) | `rc=-1 errno=103`, out zeroed |
| Native reachability | `dlsym(RTLD_DEFAULT)`, `dlopen` of libSystem/libsystem_{coreservices,info,kernel,darwin}, `libquarantine` (errno=2), `libresponsibility` (errno=2), `CoreServices`, direct `-undefined,dynamic_lookup` import | all fail; the last produced `EXC_CRASH SIGABRT` DYLD "symbol not found in flat namespace" — recorded crash artifact |

Facts R004 already establishes that newer proposals kept missing:

1. **The `__mac_syscall` (#381) entry is live on this iPadOS kernel** —
   the getter call returned a *normal* ENOPOLICY, not
   ENOSYS/SIGSYS/crash. The "does the syscall even exist on iOS"
   question is answered (FACT via R004 runtime, not THEORY).
2. **The native-token control already ran** — a genuinely native,
   launchd-parented token also returns 103 through the Ventura getter.
   "Try a native token" proposals are repeats.
3. The provider libquarantine `A2D17FF6` in the chroot is the real
   Ventura build; the stub binds to it and the call site ABI is
   verified (`x0=target token, x1=out, x2/x3=0`).
4. R004's own boundary wording is already the correct calibration:
   ENOPOLICY is *consistent with* missing kernel policy *or* a
   wrapper-contract mismatch — it cannot distinguish them, and it does
   not prove a native implementation would succeed.

## 3. Audit of the proposed `Quarantine`-vs-`Sandbox` `0xb6` A/B — unsafe as specified

The numeric selector **is not** portable across policies.
`__mac_syscall(name, op, args)` dispatches `op` into that policy's own
`mpo_policy_syscall` table — `0xb6` means "responsible-token getter"
only inside Quarantine's table. Under Sandbox the same number is an
unspecified selector whose semantics are unverified (Sandbox ops
include mutating operations: container/extension issuance, profile
ops). Running `"Sandbox", 0xb6` blind has no read-only contract —
**correctly vetoed by the coordinator**; I withdraw the D064/D065-era
suggestion to pair it as the positive control.

### Strictly safer replacement control (design only — no execution)

`__mac_syscall("<guaranteed-unregistered name>", op, args)` where the
name is an invented string that cannot strcmp-match any registered
policy (e.g. `"ZZZmacwsnop"`):

- **Zero dispatch risk by construction**: name-miss can only reach the
  `mac_base.c` no-match default arm — no policy handler ever runs, so
  nothing can mutate.
- It fixes the default arm's *signature* on this kernel: junk → 103
  establishes "name-miss ⇒ ENOPOLICY here".
- Then `("Quarantine", 0xb6, getter-args)` discriminates:
  - `Quarantine→103` AND `junk→103` → consistent with Quarantine
    name-miss (provider absent) — strengthens the hypothesis.
  - `Quarantine→(any errno ≠103)` → the call **dispatched** → a
    Quarantine-named policy IS registered → hypothesis falsified,
    registration semantics back on the table.
  - junk → e.g. `EPERM` while Quarantine → `103` → a precheck
    (`mpo_proc_check_syscall_mac`) vets names — the R009 precheck arm
    becomes the live producer; learn its errno signature.
  - junk → `103` while Quarantine → different → also proves dispatch.
- Residuals formally open either way: a registered policy could
  deliberately return ENOPOLICY internally (masquerades as name-miss);
  runtime-patched policy lists aren't visible to static image scans;
  and a Quarantine handler with a *NULL* `mpo_policy_syscall` slot
  would register the name yet never dispatch — recorded as uncovered.
- The Quarantine leg itself reuses the exact selector+arg layout the
  Ventura getter already runs (R008-verified), so it is the same
  read-only contract as R004's calls — but **repeating the getter call
  through the same wrapper adds nothing**; the only new information is
  the junk-name comparator.

## 4. Strongest current facts — and what's genuinely undecided

| Statement | Status |
|---|---|
| `__mac_syscall` #381 entry reachable on iPadOS 16.3 kernel | FACT (R004 runtime) |
| Ventura getter returns ENOPOLICY for chroot self AND live native tokens | FACT (R004 A/B) |
| iOS dyld cache lists zero libquarantine; native getter gated to platform clients | FACT (R004 + D062 cache table) |
| `"Quarantine"` policy name absent across the analyzed kernelcache image | FACT-at-artifact-level (payload linkage + runtime patching caveats stand) |
| Provider absent on the live kernel | **HYPOTHESIS** — consistent with all evidence, not closed |
| WS→tccd-system routing (map, endpoint, role) | FACT (D065) |
| `isPerSystem` role contract excludes non-system alternative for input services | FACT (D064 registry decode) |
| Whether the current WS generation's 1002 even reaches TCC | **UNOBSERVED** — the direct open question |
| Precheck-veto vs no-match producer for observed 103 | formally split (R009), junk-name control discriminates |

## 5. The existing WS observer answers the more direct question — without new scans

The dedicated sender-observation hooks already in
`mac_hooks.m:6183–6213` + `MacWSSenderObservation.c` wrap all three
levels: gate (`+0x318464`), dispatcher (`+0x3407ec`), and **impl
(`+0x340da0` = the `TCCAccessPreflightWithAuditToken` wrapper
`0x185425da0` byte-verified this round)**. The impl record already
captures the discriminating fields:

- `lazy_pre`/`lazy_post` — the cached `PreflightWithAuditToken` symbol
  pointer before/after the call (`g_macws_obs_lazy` = SkyLight
  `0x1d8ba6b50`).
- `rc` — the check's own return (`0` = deny).
- `token`, `desc`, `invocation` — sender token + service descriptor +
  chain id.

Decision table for one bounded `SLSPostEventRecord` post under the
armed sentinel (extends D042's §4 matrix with the lazy fields):

| Observation | Reading |
|---|---|
| gate fires, **no impl record** | deny authored inside dispatcher early paths (session/cache legs) — **TCC and the provider hypothesis are not on this input attempt's causal path at all** |
| impl fires, `lazy_post==0`, `rc=0` | deny = **missing TCC symbol inside WS's chroot** (fail-closed `cbz x0` leg verified at `0x185425df0`) — a *local, fixable* producer unrelated to the kernel policy question |
| impl fires, `lazy_post!=0`, `rc=0` | `PreflightWithAuditToken` ran and denied — the denial IS TCC-attributable; the tccd side should have produced an ERROR line on the same attempt (cross-witness: tccd-system stderr in the same window) → the provider hypothesis is then on the causal path and worth resolving |
| impl fires, `rc!=0` but gate denies | TCC passed; deny authored by dispatcher cache-fold — third producer |

This is strictly more direct than further provider scans: it asks
*"is TCC even being asked, and what does it answer?"* on the live
input attempt — the question the whole effort actually needs.

### Prerequisites (enumerated, no authorization granted)

- **Install state**: the three hooks arm only inside
  `loadImageCallback` at WS launch when the sentinel
  (`/tmp/macws_sender_observation` in the chroot namespace =
  `/var/mnt/rootfs/tmp/macws_sender_observation` host-side) exists
  **at WS start**. Missing runtime input: whether the *current* WS
  generation (pid 10447 lineage) logged
  `SENDEROBS-INSTALL … permitImpl=0` — a stderr-log read decides if the
  hooks are already resident.
- **If already installed**: arm sentinel → one bounded owned post
  (D041 canonical shape) → read
  `/var/mnt/rootfs/tmp/macws_sender_observation.log` via the D049
  decoder → disarm. No restart needed; live-disarm behavior already
  verified (D046: snaps stay flat).
- **If not installed**: a WS restart is required — the heavyweight
  production operation needing the full session/rollback protocol
  (composite-state capture, producer/pid witness, sentinel cleanup in
  both namespaces, rollback = remove sentinel + restart; record-only
  wrappers restore errno and never alter returns, so a resident hook
  is safe to leave, but removal is also restart-bound).
- **Authorization subtlety**: the induced post is not a TCC call, but
  SkyLight's genuine check may emit a WS→tccd request transitively —
  that is the intended observation target, and it needs explicit signoff
  distinct from "no TCC requests" since the TCC traffic would be
  WS-authored, not probe-authored.

## 6. Minimal genuinely-discriminating observations, ranked

1. **WS-side impl record** (§5): names the producing layer of the
   *current* denial — TCC-attributable or not. Cheapest if hooks are
   already resident; still bounded if a restart is required.
2. **Junk-name `__mac_syscall` A/B** (§3): discriminates
   name-miss-vs-dispatch with provably zero dispatch risk — replaces
   the unsafe Sandbox comparator. Requires the device grant; same
   helper shape as R004's.
3. **`mac_policy_register` caller enumeration in `kc_raw`** (backend's
   item): image-level closure of the registerable-policy set; blocked
   on a bvx2 decompressor — local, no device.
4. **Getter repeats**: none justified — R004 covered both token classes.

## Provenance

`SkyLight_22F82` UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`;
`tccd_arm64e` `9845D17F`. Observer source: `libmachook/
MacWSSenderObservation.c` + `mac_hooks.m:5940–6213` +
`MacWSSenderObservation.h:109–113` (offsets). R004 verbatim artifacts
cited above. No device access, no syscalls, no setter/TCC traffic, no
source/build/Git changes this round.
