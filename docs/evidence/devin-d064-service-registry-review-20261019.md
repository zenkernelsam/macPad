# D064 frontend — exact TCC service registry + legitimate request paths

Scope: local RE of `analysis/d050-tccd/tccd_arm64e` (UUID
`9845D17F-0409-3884-8A17-260C729636E5`, arm64e 22F82) +
`analysis/SkyLight_22F82`. No device contact, no TCC requests, no
setter invocation, no source/build/Git changes. This round also
**downgrades the D062/D063 provider-absence claim to HYPOTHESIS**
(preserved in-place in both docs): `CHIP=0x8103` in the img4 manifest
does not close byte-level `.bin`↔img4 payload/runtime linkage, and the
live kernel's MAC-policy list was never enumerated.

## 1. Service registry — hardcoded table, per-service flags (FACT)

The dispatch handler's service objects are built in one init function
spanning `0x35200–0x35700`. Pattern per service: `objc_alloc` →
initializer stub `0x57ce0` with a CFString name in x2 → a run of
`set<Property>:` stubs with literal `w2`/`x2` → dictionary store via
`0x560a0`. All flags are **compile-time constants in the table** —
no plist/DB provenance.

Full decode (selref stubs resolved via `0x08`-tagged selref →
methname); only flag-relevant rows shown. `w2` is the BOOL literal:

| Service | `isPerSystem` | `shouldAllowSameTeam` | Other notable flags |
|---|---|---|---|
| `kTCCServiceAccessibility` | **1** (`0x35284`) | absent | `HasParanoidSecurityPolicy=1`, `ShouldAllowUnrestrictedCheckAuditToken=1`, `AlertStyle=1`, `compositionPromoteChildAuthorizationToParent=1` |
| `kTCCServicePostEvent` | **1** (`0x352d0`) | absent | `AlertStyle=3`, `AXControlComputerAccessType=1`, `DoNotStoreDefaultAllowAccess=1` |
| `kTCCServiceListenEvent` | **1** (`0x35334`) | absent | `AlertStyle=3`, `AXControlComputerAccessType=2`, `DoNotStoreDefaultAllowAccess=1`, `Mdm_isAuthorizationDenyOnly=1`, `Mdm_allowStandardUserToSetSystemService=1`, composition parent/child set |
| `kTCCServiceScreenCapture` | **1** (`0x353f4`) | absent | `AlertStyle=3`, `AXControlComputerAccessType=3`, `CheckAllowEntitlementOnResponsibleProcess=1`, `Mdm_isAuthorizationDenyOnly=1` |
| `kTCCServiceSystemPolicyAllFiles` | **1** (`0x3560c`) | absent | `IsAccessDeniedByDefault=1` |
| `kTCCServiceDeveloperTool` | **1** (`0x356b4`) | absent | `CheckAllowEntitlementOnResponsibleProcess=1`, `AlertStyle=1` |
| `kTCCServiceAppleEvents` | absent | **1** (`0x35590`) | `hasIndirectObject=1`, `UsageDescriptionKeyName`, `helpAnchor`, `hardenedRuntimeEntitlementName` |
| `kTCCServiceFileProviderDomain` | absent | absent | `hasIndirectObject=1` |

(23 service blocks decoded in total; remaining services —
NearbyInteraction, CrashDetection, FocusStatus, UserAvailability,
LinkedIn, AppBundles, SysAdminFiles, DeveloperFiles,
FileProviderPresence, WebBrowserPublicKeyCredential, Removable/Network
Volumes, Desktop/Downloads/Documents folders — carry neither flag.)

**Decisive registry facts:**

- `macos_isPerSystem` setter callers: `0x35284` `0x352d0` `0x35334`
  `0x353f4` `0x3560c` `0x356b4` — exactly the six services above. The
  `0x66f8`/`0x670c` leg-select gate (`w28`) is TRUE for every service
  SkyLight consumes.
- `macos_shouldAllowSameTeam` setter caller: **only `0x35590`,
  on `kTCCServiceAppleEvents`.** No input service carries it.
- `macos_hasIndirectObject` is set on `AppleEvents` and
  `FileProviderDomain` — the Indirect handler's subject-token path is
  AppleEvents-shaped, not input-shaped.

RE-confirmed via `tccd_arm64e` `0x35200–0x35700` disasm + selref decode
(setter stubs `0x59fc0`/`0x5a040`/`0x59d00`/`0x59ee0`/`0x59be0` etc.
resolved to their methnames; `w2` literals read at each callsite).

## 2. Legitimate request path — the full verified chain (FACT modulo §0 hypothesis)

From the main connection handler (all offsets `tccd_arm64e`):

1. `0x66d4` `serviceFromMessage:error:` → service object from the
   message's service name. nil → `0x6824` leg.
2. `0x66f8` `service.macos_isPerSystem` → `w28`.
3. `0x6704`/`0x6828` `server.macos_isSystemServer` (argv `system` arg,
   global byte `0x8f888` → `+0x29` bit0; setter callsite `0x6108`):
   - flag=1 AND any service → `0x6838` sender attribution:
     `initWithMessage:` (`0x6848`, evaluate=YES) → `getAuditToken:`
     (kernel peer token) → `createProcessFromAuditToken:processInfo:`
     (`proc_pidpath_audittoken`; in-root senders resolve — D059) →
     `_evaluateResponsibleProcess` → libquarantine `0xb6`.
     nil builder → `0x503f4` bail → `0x69ac` ERROR + empty reply send.
   - flag=0 → `0x69e8` direct dispatch. But for `isPerSystem` services
     the `0x670c` w28 leg routes to `0x6710`, which sends **the same
     empty `x23` reply dict** (the sole `create_reply` product) and
     returns — no handler runs.
4. Dispatch `0x6ba0` → `do_TCCAccessRequest` / `…Indirect` /
   `…Preflight` by function name. Handlers produce the filled reply.

**Net legs for an input-service request:**

| tccd instance | service flag | result |
|---|---|---|
| `system` (flag=1, deployed) | any | attribution → `0xb6` → **#103 if provider absent** → empty reply |
| non-system (flag=0) | `isPerSystem=1` (all input services) | `0x6710` early-bail → **same empty reply, handler never runs** |
| non-system | `isPerSystem=0` | direct dispatch → real evaluation (irrelevant: no input service qualifies) |

**D066 correction (preserved):** my original wording below — "no leg in
which a global input request reaches a verdict-producing handler on this
deployment … unreachable by construction" — was an overclaim. The role
contract excludes only the **non-system alternative**: isPerSystem
services structurally early-bail there (FACT). The **authentic system
endpoint exists and is the legitimate path** — it is blocked only
*conditionally* on the unresolved provider hypothesis, not by
construction. If a Quarantine policy is registered at runtime, the
system route produces real verdicts and global-input synthesis is not
architecturally impossible.

Corrected statement: a global input request cannot reach a
verdict-producing handler **through the non-system alternative** (role
contract, unconditional); **through the system endpoint** it reaches a
handler iff the provider hypothesis fails. Both failing paths emit the
identical empty reply dict — `count=0` cannot distinguish them without
the ERROR line (D060 finding stands).

## 3. Indirect handler — same-team skip is subject-side, and sender attribution still precedes it

Inside `do_TCCAccessRequestIndirect` (`0x11aa4`–):

- `0x12234` reads `indirect_object_token` (32-byte audit token) from
  the **message dictionary** — the subject identity is dict-supplied.
- `0x12368` `service.macos_shouldAllowSameTeam` → `0x1238c`
  `w2 = w0 ^ 1` → chain build through helper `0x3a8e4`. For
  `shouldAllowSameTeam=1` (AppleEvents only) the subject chain is built
  with `evaluateResponsibility=NO`; for every other service, YES.
- A second chain build at `0x128e8` uses `initWithMessage:`
  (evaluate=YES) — the non-token subject branch.

**Two load-bearing orderings:**

1. The outer connection-handler attribution (`0x6848`) runs **before**
   dispatch to `do_Indirect` — a sender who fails `0xb6` never reaches
   the Indirect internals at all. The same-team skip can never rescue
   sender attribution; it only relaxes *subject-chain* evaluation for
   AppleEvents.
2. Therefore the Indirect leg is not a sender-side bypass and is not
   reachable by a `#103`-class sender — matching backend's net model,
   now with the registry fact that **only AppleEvents carries the
   flag**.

## 4. SkyLight consumer side — fail-closed at every level (FACT)

`SkyLight_22F82` consumes exactly three TCC services:
`kTCCServicePostEvent`, `kTCCServiceListenEvent`,
`kTCCServiceScreenCapture` — all `isPerSystem=1`, none `sameTeam`.

- Init (`0x18520d228`): `dlopen("/System/Library/PrivateFrameworks/
  TCC.framework/TCC")` + `dlsym("TCCAccessPreflight")` /
  `dlsym("TCCAccessRequest")` into globals `0x1db438a60/a68`; on miss it
  only logs `"Could not find TCC symbol"` — pointers stay NULL.
- Sender-check helper `0x185425da0` (reached by pointer from the
  `CGXSenderCanSynthesizeEvents` chain — no direct `bl` callers):
  lazily resolves `TCCAccessPreflightWithAuditToken` via
  `tcc_lookup_symbol` → `blraaz` calls it as
  `PreflightWithAuditToken(service=x19, auditToken=sp, options=0)`;
  returns `w0==0` ⇒ allowed. **`cbz x0` on the missing symbol falls to
  the epilogue with `w0=0` → deny. Fail-closed**, consistent with the
  observed `1002` denials and with `#103`-empty replies mapping to
  nonzero preflight results.

So the 1002 denial chain end-to-end: sender post → gate →
`PreflightWithAuditToken` → TCC.framework XPC to `com.apple.TCC.system`
→ chroot tccd → attribution builder → `0xb6` → (provider hypothesis)
#103 → empty reply → preflight nonzero → helper returns 0 →
`_XPostEventRecord` → 1002.

## 5. Global/full-Aqua input vs the accepted exact-AppInput path

- **Global path** (HID event stream / `SLSPostEventRecord` →
  `kTCCServicePostEvent`/`ListenEvent` on the sender): requires a real
  TCC verdict for an `isPerSystem` service. The non-system alternative
  is excluded by the compile-time role contract (structural FACT). The
  authentic system endpoint is the legitimate route — currently blocked
  only *if* the provider hypothesis holds (unresolved). Whether the
  current-generation WS denial even reaches TCC is itself unobserved —
  see D066 for the observer-based discriminator.
- **Exact-AppInput path** (AppInputBridge → exact PID/window delivery):
  already accepted on device (D041-era witness). It targets a specific
  owned window via the app's own connection — it does not present
  itself to SkyLight as a global event-synthesis sender and therefore
  does not traverse the `PreflightWithAuditToken` sender gate at all.
  **No duplicate transport should be built**: the AppInput route is
  the correct mechanism for delivering input to owned macOS windows;
  global synthesis is the class of operation TCC gates and this kernel
  configuration cannot service.
- Honest framing: full-Aqua *global* input (inject into arbitrary
  windows/HID taps) is unreachable while the provider-absence
  hypothesis holds; owned-window input remains available via the
  existing accepted path.

## 6. Minimal viable next step / missing runtime inputs

Nothing this round can close locally without device access:

1. **Provider hypothesis resolution** — backend's saved-kernel-linkage
   item (byte-level `.bin`↔img4 compare) plus live-policy enumeration
   (e.g. a controlled `__mac_syscall("Sandbox",0xb6)`-vs-`"Quarantine"`
   A/B from an owned helper — one syscall each, zero TCC). If
   "Quarantine" resolves, #103 provenance returns to policy-internal
   and registration is back on the table.
2. If provider presence is confirmed, the first input-service request
   would still produce the ERROR line that names the next producer
   (D060 discriminator) — no code change needed to observe it.
3. iOS `libsystem_kernel` `__sandbox_ms`/`__mac_syscall` export check
   (`dyld_info` on device cache) — the channel-stub question stays
   THEORY until then.

Explicitly NOT needed: a same-team subject path (AppleEvents-only and
post-attribution anyway), a non-system tccd (early-bails every input
service), or any duplicate input transport.

## Provenance

Binary: `analysis/d050-tccd/tccd_arm64e`,
`9845D17F-0409-3884-8A17-260C729636E5`. SkyLight:
`analysis/SkyLight_22F82`. All offsets are file addresses (`0x1000xxxxx`).
Setter-caller and selref decodes performed locally this round; block
excerpt at `0x352b8–0x35300` re-verified verbatim. No device, no TCC
traffic, no setter calls, no source/build/Git mutations.
