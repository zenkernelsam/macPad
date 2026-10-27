# D098 — consistency review of D097 grant packet (vs D069/D060/R014)

Offline audit only. No execution, device, or Git. Subject:
`devin-d097-backend-20261019.md` checked against the D068 image-level
semantics, the D060 attribution chain, and R014/D069 status.

## Edit 1 — REQUIRED: "absent ⇒ every query is structurally ENOPOLICY" is overstrong in both directions

D097's table claims: "Absent ⇒ every `responsibility_get_*`/
`__mac_syscall` query is structurally ENOPOLICY — the #103 producer
arm is *closed at source*."

Two corrections, both grounded in the D068 decode of `mac_syscall`
(`0xfffffe000865db70`):

- **Not all future failures are guaranteed 103.** The precheck loop
  calls every *registered* policy's `mpo_proc_check_syscall_mac`
  before the name-match dispatch; any registered foreign policy may
  veto with its own errno (the aggregated ladder EAGAIN/EINVAL/
  ESRCH/ENOENT/EACCES/EPERM/last-nonzero). Absent Quarantine
  guarantees only that *queries reaching dispatch* land on the
  `w21=0x67` no-match producer — a precheck veto can still produce a
  **non-103** failure.
- **#103 is not formally exclusive to no-match.** D068 explicitly
  records "a hook CAN produce ENOPOLICY — formally open": a policy's
  own precheck (or, if registered, its syscall handler) could return
  103. The no-match site is the *structural* 103 producer; it is not
  the *only possible* one.

**Replacement wording:**

> Absent ⇒ every `__mac_syscall`-routed query that reaches dispatch
> fails at the literal no-match producer (`0x865de80`, ENOPOLICY) —
> for the whole boot epoch (`mac_late=1`). This does not make every
> future failure 103 (a foreign precheck veto may return another
> errno), and it does not *formally* prove historical #103s were
> no-match products — a hook returning 103 is not excluded, and the
> `mac_policy_unload` path is unexamined (built-in policies assumed
> persistent).

That last caveat matters: under absent+same-epoch the retrospective
claim is *strong*, not total — "no-match is the structurally
indicated producer for every #103 this epoch" is supportable;
"proven" is not.

## Edit 2 — REQUIRED: downgrade "userspace registration never exercised"

D097/D096 state the registration leg "is never exercised" based on
project-source + SkyLight-import audits. Audit scope is narrower
than the claim:

- Audited: project sources (zero `responsibility_*`/`TCCAccessSet*`
  callers), SkyLight 22F82 imports (zero).
- **Not audited**: Ventura `launchd` (the natural
  `responsibility_spawnattrs`/job-claim caller — device-only),
  Ventura `tccd` (device-only), and the rest of the chroot process
  set. Any of them could call `TCCAccessSetPidResponsibleForPid` or
  spawn-attrs for jobs — including for our targets — and leave no
  trace in the two audited surfaces.
- Empirical support (D060/R003): launchd-hosted probes still got
  `#103` + empty replies — *consistent with* no effective
  registration, not proof of absent registration.

**Replacement wording:**

> No auditable caller exercises the registration leg (project
> sources + SkyLight imports both zero). Ventura `launchd`/`tccd`
> and other chroot system images are outside audit scope — a
> registration caller there cannot be excluded; observed #103s are
> consistent with, not evidence for, absent registration.

## Edit 3 — recommendation: split "epoch" vs "generation" wording once

The proof table conflates them once ("boot epoch (`mac_late=1`)");
elsewhere "generation" is used for WS/desktop. Keep one line:
kernel policy epoch = boot-scoped (outlives WS generations); WS
generation = the witness unit. Prevents a future reader mapping the
snapshot's epoch claim onto a WS-generation bound.

## Verified correct (no change)

- `PROC_PIDRESPONSIBLE` absent from `proc_info.h`; `p_responsible_pid`
  write/inherit/kcdata-only — procinfo leg correctly ruled
  unimplementable.
- Smallest-discriminator logic holds: only the policy table decides
  provider-registered-vs-absent.
- Scope/exclusions/pinned hash unchanged and consistent with
  D069–D090.

## Bottom line

Two wording edits required (overstrong ENOPOLICY guarantee;
overstrong never-exercised claim), one clarifying line recommended.
No defect in the grant mechanism, hash binding, or scope. Apply as
revision-5/errata before presentation.

(handover to codex now)
