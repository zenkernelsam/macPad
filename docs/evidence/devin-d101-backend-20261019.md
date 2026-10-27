# D101 — errata on D100 getter-semantics inferences

Offline disassembly audit only. No device, probes, setter invocation,
TCC/KRW syscalls, builds, source edits, or Git writes. Subject:
`devin-d100-backend-20261019.md` (tccd_22F82_live, sha256
`42c388e8…`, arm64e UUID `9845D17F…`).

## Erratum 0 — address transcription (already fixed in D100)

Three setter-handler VAs in D100 dropped a digit; corrected in place:
`0x10008c70`→`0x100008c70` (handler), `0x10008d84`→`0x100008d84`
(NSError branch), plus the `0x100008cbc`/`0x100008d10` literals. No
semantic change.

## Erratum 1 — "absent entry returns self" is THEORY, downgraded

D100 said: `"No responsible registered" under a functioning provider
is not an error — it surfaces as rv==0 with responsible == caller`.
What the binary actually proves is narrower:

- **Proven**: the caller has a self-responsible branch — rv==0 AND
  `pid(caller_token)==pid(responsible_token)` AND
  `binaryPath` equal → `_responsibleProcessIsValid=1`,
  `_responsibleProcess=nil`, ret 1 (`0x10004fc48` `cmp` →
  `b.ne` diverges to the distinct path; fallthrough compares
  `binaryPath`/`isEqualToString:` at `0x10004fc50`–`0x10004fc98`).
- **Not proven**: that a *registered* provider returns a self-token
  for unregistered processes. That behavior lives in the provider's
  `mpo_policy_syscall` handler — kernel-side, closed-source Quarantine
  policy, not in this xnu tree and not in this binary — plus the
  libsystem_kernel wrapper (`responsibility_get_responsible_
  audit_token_for_audit_token` is an unresolved `U` import; Ventura
  libsystem_kernel is absent locally). **Label: THEORY.**
- Structural corollary that DOES hold: provider *absent* (ENOPOLICY)
  and provider *present-but-erroring* are indistinguishable to this
  caller — both land in the same rv!=0 tail. "Absent entry" and
  "absent provider" are different cases; only the former could even
  hypothesize self-token success.

## Erratum 2 — errno: control flow flattened ≠ errno unrecoverable

D100 overclaimed: "userspace observation cannot recover which kernel
errno fired". Corrected, three layers:

- **The errno value IS captured**: the failure tail's logger helper
  (`0x1000519e0`) calls `__error()` twice (`0x100051a14`, `0x100051a1c`)
  → `errno` + `strerror(errno)` formatted into `"ERROR: responsibility_
  get_responsible_audit_token_for_audit_token() from PID[%d]: (#%d)
  %{public}s"` via `_os_log_error_impl`. If the error log level is
  enabled and logs are retrievable, the errno number is observable.
- **Caveat on value fidelity**: between the getter's return
  (`0x10004fb48`) and the `__error()` reads sit ~8 calls
  (`0x100056c00`, `retainAutoreleasedReturnValue`, `0x1000596c0`,
  `0x100058440`, `objc_release`×2, `os_log_type_enabled`, then
  `[x19 pid]` inside the helper). The logged value is errno
  *at read time*; the binary does not prove it equals the getter's
  errno (typical `objc_msgSend`/`os_log_type_enabled` do not set
  errno, but that is convention, not disasm-provable).
- **errno value ≠ producing branch**: even a correct 103 cannot
  distinguish the no-match preset (`0xfffffe000865de80`) from a
  policy hook returning 103 — the D068/R014 caveat. The snapshot
  question stays exactly where v3 put it.
- What remains true: the daemon's **control flow** is errno-blind —
  one `cbz w0` gate, identical tail for every nonzero rv. So the
  daemon's *behavior* can't distinguish failure classes; the *log*
  can carry the errno value.

## Setter dispatch/entitlement gates — verified at exact addresses

- Dispatch: `strcmp(opname,"TCCAccessSetPidResponsibleForPid")` at
  `0x10006cf4`; match → `ldr x0,[x24,#0xb30]` → `macos_isSystemServer`
  (`0x10006d08`–`0x10006d10`, `cbz` → `0x10007b30` skip) →
  `bl 0x100008c70` at `0x10006d1c`.
- Handler entitlement gate: callsite `0x100008cc8` invokes helper
  `0x10003a604` with `x1` = literal@`0x10007d6a8` → **__cfstring
  `com.apple.private.tcc.manager.set-responsible` (len 45)**, `w2=1`;
  `tbz w0,#0` → `0x100008d84` NSError branch. The helper's own literal
  @`0x10007d698` → __cfstring `com.apple.private.tcc.` (prefix used
  inside the SecTask check path).
- Existence verified only — this maps the gate; it does not
  authorize invoking anything.

## Net effect on the grant chain

No change to the v3 packet's mechanism or rationale; the evidence
model is tightened: (a) self-responsible-success on absent entry is
explicitly THEORY pending a provider/wrapper artifact; (b) the errno
value is recoverable via the error log (with the read-time caveat),
while kernel-branch attribution remains unrecoverable from userspace;
(c) the setter endpoint is now precisely gated at named addresses.

(handover to codex now)
