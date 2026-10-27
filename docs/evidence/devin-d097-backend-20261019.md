# D097 — smallest grant text: one read-only responsibility discriminator

Offline document only. Reviews D060 (attribution chain), D069 (frozen
runner + inadmissible run), D096 (upstream split). Runner bytes/hash
unchanged. **NOT APPROVED** — grant requires the named authorization.

## First: the two concepts are not interchangeable

**procinfo responsibility fields** — `p_responsible_pid`, a
per-process kernel field. In `analysis/xnu-xnu-8792.81.2` (adjacent
patch level to the device's 8792.82.2): written only by
`proc_set_responsible_pid` KPI (`kern_proc.c:4698–4706`), inherited
at fork (`kern_fork.c:1096`), and surfaced only via crash-time kcdata
(`TASK_CRASHINFO_RESPONSIBLE_PID`, `kern_exit.c:698–699`). **No
`proc_pidinfo` flavor exists for it** (`proc_info.h` has no
`PROC_PIDRESPONSIBLE`) — so "read the responsibility field via
procinfo" is *not an implementable primitive* on this kernel. Any
per-process field read would require either the
`responsibility_get_*` API (which itself routes through the
Quarantine policy — circular as a discriminator) or a KRW read of
the proc struct (a different packet, KRW-scoped).

**`mac_policy_list` Quarantine snapshot** — the policy-table read:
whether a `Quarantine`-named MAC policy is registered and whether its
`ops+0x388`/`ops+0x3a8` slots are populated. This is the question the
frozen runner v3 exists to answer.

## The grant (verbatim)

> **Authorization requested: `macws-policy-snapshot-v3`**
>
> Runner v3, sha256
> `8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`,
> under the D088 rev-4 + D090 errata contract (one attempt, numbered
> steps, dual whitelists, sanitized output, fail-closed terminals,
> `physrw-unarmed`→`missing-authorization`, no helper/trustcache/
> restart/injection/capture/sentinel).
>
> **One discriminator only**: the bounded read of `mac_policy_list`
> header and entries. No `proc_pidinfo`, `proc_info`, kcdata, or
> proc-struct reads are added — those are out of this packet's scope
> and, per the source audit above, no procinfo flavor exists for the
> responsibility field anyway.

## What each read can and cannot prove

| Read | CAN prove | CANNOT prove |
|---|---|---|
| **Quarantine snapshot** (this grant) | Whether a `Quarantine`-named policy is registered at the snapshot instant; whether its precheck/dispatch slots are populated. If absent, a query that reaches name dispatch has no Quarantine match and takes the literal no-match ENOPOLICY producer (`0x865de80`); a foreign-policy precheck may still return another errno first. Policy-table state is boot-epoch scoped (`mac_late=1`), not tied to a WindowServer generation. | Which producer fired for historic `#103`s; whether a hook returned `#103` (formally open); whether any process carries a responsible pid; whether a setter was ever called; per-process state; TCC policy evaluation outcomes |
| **procinfo responsibility-field read** (not in this grant; not implementable as procinfo) | *(If it existed:)* the recorded per-process `p_responsible_pid` value; a value alone does not establish who set it or whether it was inherited | Policy registration; why ENOPOLICY fires; anything about the API path. As a KRW proc-struct read it could still show field values — different packet, different scope |

## Why this is the smallest discriminator

- Audited project sources and SkyLight imports contain no
  `TCCAccessSetPidResponsibleForPid`/`responsibility_*` caller. Ventura
  launchd/tccd and other chroot system images are outside that audit, so a
  registration caller there cannot be excluded. Existing `#103` observations
  are consistent with absent effective registration, not proof of it.
- The policy snapshot answers only the independent kernel-policy question:
  whether a Quarantine-named provider is registered for this boot epoch.
  Registration-vs-query-resolution and the exact historical `#103` producer
  remain unresolved; the snapshot does not collapse those branches.
- That question is decided by exactly one observation: the policy
  table. No userspace read can substitute (procinfo leg is
  unimplementable; tccd replies are already observed empty).
- The packet is already frozen, reviewed (D077–D090), and bound to a
  hash — no smaller remaining preparation exists.

## Explicitly out of scope

Everything the rev-4 + errata text excludes, plus: any proc-struct
KRW read, any procinfo call, any responsibility setter or getter
invocation, any TCC probe. A future per-process field read would be a
separately named grant.

(handover to codex now)
