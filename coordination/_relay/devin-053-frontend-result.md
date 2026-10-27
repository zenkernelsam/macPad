# devin-053-frontend — native-sender & ENOENT attribution audit RESULT

**Doc:** `docs/evidence/devin-d053-native-sender-independent-review-20261019.md`
**Artifacts reconciled:** `analysis/devin-d052/` — sender src `77dd7d67`,
launchd plist (user/501, `/var/jb/tmp` program, no UserName→root, no
KeepAlive), device capture + oslog segment, signed bin `1cc8636e`.
**Device/backend/Git:** untouched; no resend; D052 backend completed
before this review.

## Verified against saved evidence

- Request well-formed on the wire (7 keys echoed verbatim; target_token
  matches raw `task_info` `auid=-1 euid=0 pid=27108`); reply
  `dict count=0` = the unconditional empty `x23`.
- Stage moved per server ERRORs: chroot `#103 responsibility` → iOS
  sender `proc_pidpath_audittoken() (#2) ENOENT`. Absent `#103` is
  **indirect** stage-1-pass evidence (ERROR absent + stage-2 ERROR
  present); getters remain `SYMBOL_MISSING` client-side — labeled, not
  substituted.
- 3 pre-send crashes (dlsym PAC fault, xpc_copy_description null-node,
  `&_xpc_type_null` misuse) = helper failures; no request left the
  process in any.

## Independent corrections

- **`sender_auid=501` = interpose artifact, cross-validated both ways**:
  Ventura tccd prints 501 for an `auid=-1` sender via interposed
  `audit_token_to_auid` (`mac_hooks.m:13721`, stub `0x65b8`); native
  tccd[883] prints the genuine `-1` on the same log statement.
  "Connection-inferred from domain" superseded; `auid=-1` on a stock
  platform daemon retires auid as the differing axis entirely.
- **ENOENT site**: `0x4b9d8` `proc_pidpath_audittoken(&token,buf,0x400)`
  in the `TCCDProcess` identity ctor — kernel vnode→path resolution of
  the sender's exec textvp. **ENOENT alone does not establish bind vs
  trust-vs-unlink** — producer set enumerated in doc §3.
- **A/B flaw**: `/var/jb/tmp/X` ≡ its preboot backing path = the SAME
  vnode — path-spelling A/B exercises identical textvp, discriminates
  nothing. A different vnode (different fs location) confounds vnode vs
  trust context, and `/private/var/mobile` exec-admission is unverified
  (D048: `/var/jb` trusted, `/tmp` not even with cdhash).

## Minimal next observation — zero TCC traffic

Self-call `proc_pidpath_audittoken` from a `/var/jb`-exec'd helper (no
launchd job needed): ENOENT → kernel can't resolve bind-exec'd vnodes
generically (fix = canonical-path *install*, not spelling); path
resolved → ENOENT is tccd-context-specific → reopen server-side
analysis. Optional same-helper widening: call on a `/var/jb` daemon pid
vs platform pid.

## Classification rule (now evidence-backed)

`count=0` ⇒ pre-Result attribution bail; `auth_error_code` present ⇒
reached identity stage (native reference refused identity yet still
returned populated 6-key reply).

(handover to codex now)
