# D100 — exact local `tccd_22F82_live` responsibility-producer audit

Offline exact-binary audit only. No device, no TCC/KRW invocation, no
builds, no production edits, no Git. This **corrects** the D096/D097
claim that Ventura `tccd` is absent locally — the exact image is
present and identity-verified below. Ventura `launchd` remains absent.

## Image identity

| Property | Value |
|---|---|
| File | `analysis/d050-tccd/tccd_22F82_live` (fat x86_64+arm64e; arm64e slice at fat fileoff 688128, size 654192) |
| SHA256 | `42c388e875f5a479402a3a8318aa9c866101c1162d042ba0daafbf5d996c828d` (verified, matches task pin) |
| arm64e UUID | `9845D17F-0409-3884-8A17-260C729636E5` (verified via `otool -arch arm64e -l`) |
| Thin slice | `analysis/d050-tccd/tccd_arm64e` — same UUID; extracted arm64e slice (654192 B) |
| Offset convention | all VAs are arm64e-slice VAs (`0x100000000`-based); thin-slice fileoff = VA − `0x100000000`; fat-file off = thin off + 688128 |

Artifacts (verbatim, both conventions):
- `analysis/d050-tccd/d100/d100-disasm.txt` — 540 lines otool `-tvV`
  sha256 `633019e24e5872b8d60e1d48fc9762f79383aa591ed5e46342edb45fc85d1041`
- `analysis/d050-tccd/d100/d100-raw-bytes.txt` — raw instruction words
  sha256 `029ef7ca12c1ed19fea1813809d4411f1cccdf35e04ba5b1ad7195e118ccf9ce`

## tccd's responsibility surface (all imports verified)

`responsibility_get_responsible_audit_token_for_audit_token`,
`responsibility_get_pid_responsible_for_pid`,
`responsibility_get_attribution_for_audittoken`,
`responsibility_set_pid_responsible_for_pid`,
`responsibility_identity_get_{binary_path,binary_signing_id,
binary_entitlement_data,platform,sdk,csflags}`,
`responsibility_identity_release`. So tccd is **both** a consumer of
the Quarantine-provider query path **and** the registrar endpoint that
invokes the setter — the daemon does not go through the client-side
`TCCAccessSetPidResponsibleForPid` (it calls the libsystem
responsibility primitive directly).

## `-[TCCDAttributionChain _evaluateResponsibleProcess]` @ `0x10004fac4`

Identity verified through the relative method list, not inferred:
class_ro `0x10008c618` (name→`"TCCDAttributionChain"`), method entry
`0x10005d6e0` = {name→selref `0x10008cac0`→`_evaluateResponsibleProcess`,
types→`0x100079134`, imp→`0x10004fac4`}. Ivar map (list `0x10008c648`):
`+0x8 _accessingProcessSpecified`, `+0x9 _accessingProcessIsValid`,
`+0xa _responsibleProcessIsValid`, `+0xb _responsibleProcessIsDistinct`,
`+0x10 _accessingProcess`, `+0x18 _responsibleProcess`,
`+0x20 _requestingProcess`.

### Flow (RE-confirmed via `d100-disasm.txt`)

1. `__error(); *errno=0` (`0x10004fafc`–`0x10004fb00`) — errno is
   explicitly cleared before the getter.
2. `x19 = [self accessingProcess]`; caller token =
   `[x19 auditToken]`→`sp+0x40` (all-zero token if x19 nil).
3. `rv = responsibility_get_responsible_audit_token_for_audit_token
   (&caller@sp+0x40, &responsible@sp+0x60, 0, 0)` @ `0x10004fb48`.
   Args x2/x3 = 0.
4. **Single gate `cbz w0 → 0x10004fbc4`** (`0x10004fb4c`). Any nonzero
   rv → one identical failure tail:
   - conditional `_os_log_error_impl` via helper `0x1000519e0`, which
     reads `__error()` twice → `errno` (`w21`) and `strerror(errno)` →
     format `"ERROR: responsibility_get_responsible_audit_token_for_
     audit_token() from PID[%d]: (#%d) %{public}s"`. **errno is
     captured for the log string only** — it is never compared or
     branched on anywhere in the function.
   - `_responsibleProcessIsValid (+0xa) = 0`;
     `_responsibleProcess (+0x18) = nil` (old value released);
     return `w22 = 0`.
5. Success path (`rv==0`):
   - `x20 = [[TCCDProcess alloc] initWithAuditToken:&callerToken]`;
     nil → error tail (helper `0x1000518f0`) → ret 0.
   - `pid(callerToken)` vs `pid(responsibleToken)`:
     - **differ** → `_responsibleProcess = [[TCCDProcess alloc]
       initWithAuditToken:&responsibleToken
       responsibleIdentity:x20]`; success → `*(u16)+0xa = 0x101`
       (IsValid=1, IsDistinct=1), ret 1; nil → +0xa=0, log, ret 0.
     - **equal** → `[x20 binaryPath] isEqualToString:[x19 binaryPath]`;
       mismatch → same distinct-responsible branch; match →
       **self-responsible**: IsValid=1, `_responsibleProcess`=nil,
       ret 1 — and **only if `geteuid()==0`**:
       `[self _prettyGoodGuessForResponsibleProcessesForPrivileged
       HelperProcess:x19]` → `objectAtIndexedSubscript:0` → stored as
       `_responsibleProcess` (IsDistinct=1), then
       `responsibility_set_pid_responsible_for_pid([x19 pid],
       [x23 pid])` + `responsibility_get_pid_responsible_for_pid
       ([x19 pid])` read-back, logged via `"Making %@ responsible for
       Privileged Helper %@; PID[%d]; rv = %d"`. **Setter rv is not
       validated** — the function still returns 1.

### Answer to the D100 question

**The exact caller does NOT distinguish absent entry, provider
failure, or malformed output.** There is exactly one branch on the
getter (`cbz w0`); errno discrimination exists only inside the
error-logging helper, not in control flow. "No responsible
registered" under a functioning provider is not an error at all — it
surfaces as rv==0 with responsible == caller → the self-responsible
branch. Malformed output is unguarded: `sp+0x60` is trusted verbatim
on rv==0 (no zero-token or sanity check; only a downstream
`initWithAuditToken:` nil would fail later). ENOPOLICY, EINVAL, or
any other provider error produce byte-identical control flow.

## Setter daemon endpoint (`handle_TCCAccessSetPidResponsibleForPid` @ `0x100008c70`)

Reached from the strcmp dispatch at `0x10006cf4` on opname
`"TCCAccessSetPidResponsibleForPid"`, **double-gated**:

1. `macos_isSystemServer` (`0x10006d08`, `cbz` skip) — only the
   system-server tccd handles it; consistent with the D065
   `com.apple.macosbooter.tccd.system` split.
2. Entitlement check `0x10003a604` (`xpc_dictionary_get_audit_token`
   → `SecTaskCreateWithAuditToken` → entitlement value test; `tbz`
   fail → NSError branch `0x100008d84`). The entitlement string
   `com.apple.private.tcc.manager.set-responsible` is present in the
   binary.

Handler body: reads `pid`/`responsible_pid` int64 keys;
`responsible_pid == 0` → `csel` defaults it to `pid`; calls
`responsibility_set_pid_responsible_for_pid(pid, rpid)`; logs
`"responsibility_set_pid_responsible_for_pid(%d, %d): %d"`; replies
`xpc_dictionary_set_bool(reply, "result", rv==0)` — **the client sees
only a bool; rv/errno are not propagated.**

## Third site — identity initializer @ `0x100022d18`

`initWithAuditToken:`-style initializer (objc_msgSendSuper2 init,
token stored at `self+0x10`): calls
`responsibility_get_attribution_for_audittoken(&token, 0)` →
identity handle at `self+0x8`; **NULL identity → returns nil** (log
via `0x1000230e0`). The `responsibility_identity_get_*` stubs at
`0x100022e50`–`0x100023050` are the attribute-accessor surface
(binary_path, signing_id, entitlement_data, platform, sdk, csflags).

## Binary-provable vs runtime-required

**Proven from this binary:**
- tccd imports and exercises the full responsibility getter+setter
  surface; `_evaluateResponsibleProcess` consumes
  `responsibility_get_responsible_audit_token_for_audit_token`.
- The getter caller applies one `cbz` gate — no errno discrimination;
  nonzero rv → `IsValid=0`, responsible=nil, ret 0.
- The setter endpoint exists, entitlement+system-server gated, and
  reports only a bool to the client.
- "Absent registration" ≠ error in the caller's model: a working
  provider with no entry returns self-responsible (rv==0).

**Requires runtime (not decidable offline):**
- The actual rv/errno at the getter on the live kernel (ENOPOLICY=103
  vs something else) — the snapshot question stays with v3.
- Whether `macos_isSystemServer` and the `set-responsible`
  entitlement pass on the live system tccd.
- Whether any code path actually reaches `_evaluateResponsibleProcess`
  at runtime (invocation frequency is a dispatch/runtime fact, not
  provable from statics — the function is reachable through the
  `TCCDAttributionChain` init chain `initWithMessage:
  evaluateResponsibility:` whose thunks sit at `0x10004ffa8`–
  `0x10004ffb7`).

## Consequence for the packet chain

D097's grant text stands — the kernel `mac_policy_list` snapshot is
still the only read-only discriminator for provider registration, and
this audit strengthens it: the daemon's getter caller treats every
nonzero rv identically, so **userspace behavior cannot recover which
errno the kernel produced**; only the provider table (v3 snapshot) or
a syscall-level probe can. The "registration leg" now has a concrete
daemon endpoint (`0x100008c70`), but D096's scoped conclusion survives:
no caller *inside SkyLight or project sources* invokes it, and the
endpoint's liveness depends on entitlement + system-server gates that
remain runtime questions.

(handover to codex now)
