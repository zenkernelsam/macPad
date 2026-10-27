# D102 — exact token-identity flow through `TCCDAttributionChain`

Offline exact-binary audit only on `tccd_22F82_live` (sha256
`42c388e8…`, arm64e UUID `9845D17F…`; thin slice `tccd_arm64e`
`039f64b7…`). No device, probes, setters, TCC/KRW, builds, edits, Git.
Artifacts: `analysis/d050-tccd/d102/d102-disasm.txt` (679 lines,
sha256 `a31e04ab…`), `d102-raw-bytes.txt` (`f15ded33…`).

## Class/method map (verified via relative method lists)

| Method | IMP |
|---|---|
| `TCCDAttributionChain initWithMessage:` | `0x10004ffa8` thunk → `evaluateResponsibility:YES` |
| `…:evaluateResponsibility:` | `0x10004ffb0` thunk → `processInfo:nil` |
| `…:evaluateResponsibility:processInfo:` | `0x10005001c` — real init |
| `getAuditToken:fromMessage:` | `0x10004ffb8` thunk → `xpc_dictionary_get_audit_token` |
| `createProcessFromAuditToken:processInfo:` | `0x10004ffc4` |
| `accessingProcess` / `responsibleProcess` getters | `0x1000505d4` / `0x10005061c` |
| `_evaluateResponsibleProcess` | `0x10004fac4` (D100) |
| `TCCDProcess initWithAuditToken:responsibleIdentity:` | `0x10004b8a0` |
| `TCCDResponsibilityIdentity initWithAuditToken:` | `0x100022d18` |

## Which token reaches the getter — traced

`initWithMessage:evaluateResponsibility:processInfo:` (`0x10005001c`,
args: x2=msg `x19`, x3=evaluate `w22`, x4=processInfo `x20`):

1. `getAuditToken:fromMessage:` → `xpc_dictionary_get_audit_token`
   (thunk `0x10004ffb8`) → **kernel-attested sender token** →
   `createProcessFromAuditToken:` → `setRequestingProcess:` →
   **`_requestingProcess` (+0x20) = the message's audit-token
   sender**. Nil → init bails (`0x1000503f4`).
2. Then dict-payload probing for the *subject*: `target_token`
   (`0x100050110`) else `indirect_object_token` (`0x10005013c`) else
   credential dict `TCC_MSG_REQUEST_AUTHORIZATION_SUBJECT_CREDENTIAL_
   DICTIONARY_KEY` requiring `TCCD_MSG_CREDENTIAL_AUTHENTICATOR_TYPE_
   KEY==1` → `…AUTHENTICATOR_AUDIT_TOKEN_KEY` (`0x100050308`–
   `0x100050378`). All three feed `xpc_dictionary_get_data` with a
   **`len==0x20` gate** (`0x10005017c` `cmp x8,#0x20` → mismatch →
   error-log path `0x1000501cc`) → `createProcessFromAuditToken:` →
   stored at **`_accessingProcess` (+0x10)**. So accessingProcess =
   **client-supplied 32-byte token from the message payload** — not
   the kernel-attested connection/sender identity.
3. `accessingProcess` getter (`0x1000505d4`): returns +0x10, **falls
   back to `requestingProcess` when nil** (`0x1000505f4`→
   `bl requestingProcess`). `responsibleProcess` getter
   (`0x10005061c`) likewise falls back to `accessingProcess`.

**Therefore the token passed to
`responsibility_get_responsible_audit_token_for_audit_token`
(`0x10004fb48`) is `[accessingProcess auditToken]` = the
dict-declared subject token when present, else the kernel-attested
sender's.** The kernel receives the *declared subject's* token, and
the requester identity is preserved separately at +0x20.

## Which returned token is used

The getter's out-token at `sp+0x60` is only consumed on rv==0:
`audit_token_to_pid(sp+0x60-copy)` vs `audit_token_to_pid(accessing
token)`; equal-pid+`binaryPath` equal → self-responsible (+0x18=nil,
+0xa=1); else `TCCDProcess initWithAuditToken:&responsibleToken
responsibleIdentity:` (where responsibleIdentity = a
`TCCDResponsibilityIdentity` built from the **accessing** token —
`0x10004fbc8` classref `TCCDResponsibilityIdentity`, init
`0x100022d18`) → stored `_responsibleProcess` (+0x18).

## Identity validation — what is and isn't checked

- **PID**: `audit_token_to_pid` on both tokens (the only direct
  token-field comparison in evaluate); `audit_token_to_au32` inside
  `initWithAuditToken:responsibleIdentity:` re-extracts pid →
  `setPid:` (`0x10004b96c`).
- **auid/euid**: `audit_token_to_au32(token, &auid@sp+0x38,
  &euid@sp+0x34, NULL,NULL,NULL, &pid@sp+0x3c, NULL,NULL)` —
  `auid==-1` → falls back to euid (`0x10004b95c`–`0x10004b968`).
- **pidversion / audit-session (asid)**: `audit_token_to_pidversion`
  and `audit_token_to_asid` are imported but have **no callsite in
  init/evaluate** (all sites at `0x100018ae8`/`0x100018b48`/
  `0x100019680`/`0x10001d034` are in unrelated functions); au32 is
  called with `asidp=NULL`. **Pid-reuse and session identity are not
  validated on this path.**
- **path**: `proc_pidpath_audittoken(&token, buf, 0x400)` must return
  `>0` — failure → init returns **nil** (`0x10004b9d4`–`0x10004b9e0`
  → `0x10004ba90` tail). `stringWithUTF8String:` +
  `stringByResolvingRealPathWithError:` produce the stored path
  (+0x50); nil path string is *logged but tolerated* (returns self).
  `binaryPath` `isEqualToString:` is the same-binary check in
  evaluate (`0x10004fc50`–`0x10004fc98`).
- **code-signing / entitlements**: `_initializeUsingTaskForAuditToken:`
  (`0x10004c7a4`, reached when responsibleIdentity==nil) calls
  `csops_audittoken` → `SecTaskCreateWithAuditToken` →
  `SecTaskCopyValuesForEntitlements` + `SecTaskCopySigningIdentifier`
  — the Security-framework identity leg; its failure also → nil.
- **responsibility identity**: `TCCDResponsibilityIdentity
  initWithAuditToken:` calls
  `responsibility_get_attribution_for_audittoken(&token, 0)` → NULL
  → **returns nil** (`0x100022d78`).

## Nil-identity producers are DISTINCT from the getter producer

Three separate failure producers inside `_evaluateResponsibleProcess`,
each with its own log tail — **do not conflate** with the observed
`#103`:

| Producer | Site | Symptom class | Log helper |
|---|---|---|---|
| **P0**: getter rv != 0 (errno class — the `#103` arm) | `0x10004fb4c` `cbz` fail → `0x10004fb50` | `ERROR: responsibility_get_responsible_audit_token… (#%d)` errno+strerror | `0x1000519e0` |
| **P1**: `TCCDResponsibilityIdentity` nil (attribution identity NULL — `responsibility_get_attribution_for_audittoken`, or bad token bytes) | `0x10004fbf0`/`0x10004fc1c` `cbz → 0x10004fe58` | different error log | `0x1000518f0` |
| **P2**: responsible `TCCDProcess` init nil — inside it: `_initializeUsingResponsibleIdentity:`/`_initializeUsingTaskForAuditToken:` fail OR `proc_pidpath_audittoken ≤ 0` | `0x10004fe44` `cbz → 0x10004fec0` | different error log | `0x100051968` |

All three end `IsValid=0`/`ret 0` (or nil chain bail) → same
empty-reply shape downstream — matching D060's "builder bailed for
another reason (path, token format)" arm: **empty reply + `(#103)`
line is specifically P0; empty reply alone is P1/P2-class**. The
dict-token `len != 0x20` error (`0x1000501cc`) is yet another earlier
producer (malformed subject token, pre-getter).

## D060 reconciliation

Path-first ordering confirmed: `initWithMessage:` (attribution build)
runs **before** the `function` strcmp dispatch — serviceFromMessage →
flag gates → chain init → dispatch. The responsibility leg fires
inside init only when `evaluateResponsibility==1` AND `processInfo==nil`
(`0x100050240`–`0x100050250`); a non-nil `processInfo` skips the
getter entirely — a pre-resolved bypass arm worth remembering when
interpreting future `skipped` vs `failed` observations.

## Open items (runtime-required, unchanged)

- Provider-side semantics for absent entries — THEORY per D101.
- Whether our senders' messages carry `target_token`/
  `indirect_object_token`/credential dicts at all (message-shape
  fact, not provable from tccd).
- Whether `evaluateResponsibility:` is ever passed 0 or `processInfo`
  supplied for our request types.

(handover to codex now)
