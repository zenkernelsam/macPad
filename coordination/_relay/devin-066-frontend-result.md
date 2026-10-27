# D066 frontend result — role correction, R004 reuse, observer discriminator

Local-only. No device, no TCC/syscall/setter, no source/build/Git
changes. Evidence: `docs/evidence/devin-d066-role-evidence-
independent-review-20261019.md`.

## Corrections (history preserved in-place)

- D064 "unreachable by construction" → overclaim: role contract
  excludes only the *non-system alternative* (FACT). The authentic
  system endpoint exists and is legitimate — blocked only
  conditionally on the provider hypothesis. Global-input impossibility
  NOT proven. Applied to D064 doc §2/§5.
- Backend D065's parallel "by role contract, independent of provider"
  claim carries the same conflation — flagged for backend correction.
- `Sandbox 0xb6` blind A/B withdrawn: numeric selectors are
  policy-specific (Quarantine's getter selector means nothing under
  Sandbox; Sandbox ops include mutating operations) — no read-only
  contract, coordinator veto upheld.

## New vs already-tested (R004 reuse)

- Already tested: chroot getter × (chroot self token, **live native
  token**) → both `errno=103 "Policy not found"`; native getter
  unreachability exhausted (dlsym/dlopen/flat-namespace SIGABRT
  artifacts). No getter repeat justified.
- R004 already proves `__mac_syscall` #381 is live on the iOS kernel
  (normal ENOPOLICY return, not ENOSYS) — the entry-existence question
  is answered FACT, not THEORY.
- Never tested: a guaranteed-unregistered policy name control;
  precheck-veto errno signature; `mac_policy_register` caller
  enumeration (bvx2-blocked, backend).

## Safer discriminator (design only)

`__mac_syscall("ZZZmacwsnop", op, args)` — a name that cannot
strcmp-match any registered policy: provably zero dispatch risk,
fixes the no-match default's errno signature. Paired with
`("Quarantine", 0xb6)` (same verified getter contract): differing
errno ⇒ Quarantine dispatched ⇒ provider present; both 103 ⇒ name-miss
consistent (residual: handler could self-return 103 — recorded).

## WS observer — answers the direct question

The dedicated sender-obs hooks already instrument the TCC boundary
itself: impl hook wraps `audit_token_check_tcc_access` (`+0x340da0` =
`0x185425da0`, byte-verified) and records `lazy_pre/lazy_post` (the
`PreflightWithAuditToken` resolved pointer) + `rc` + token + desc.
One bounded post under the armed sentinel discriminates: no impl
record ⇒ deny is pre-TCC (provider irrelevant to input);
`lazy_post==0` ⇒ deny = missing-symbol fail-closed (local fixable
cause); `lazy_post!=0, rc=0` ⇒ TCC asked and denied ⇒ provider
hypothesis on the causal path, with a same-window tccd ERROR line as
cross-witness.

Prerequisites enumerated: hooks arm only at WS launch when the
chroot sentinel exists — current generation's `SENDEROBS-INSTALL`
status is the missing read; if not resident, a full WS
restart/rollback protocol is required (design only, no authorization).
The induced post is not a TCC call but may transitively emit one —
flagged for explicit signoff.

(handover to codex now)
