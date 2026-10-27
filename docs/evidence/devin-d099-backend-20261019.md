# D099 — final cross-review of edited D097 grant text

Offline review only. No execution, device, runner, or Git. Subject:
user-edited `devin-d097-backend-20261019.md` verified against D060
(attribution chain), D068 (image-level semantics), D069 (frozen runner +
inadmissible run), R014 (`r014-responsibility-capture-plan-20261018.md`).

## Required points — all verified

**1. Boot-epoch vs WS generation — consistent.**
Line 49: "Policy-table state is boot-epoch scoped (`mac_late=1`), not
tied to a WindowServer generation"; line 60 reuses "for this boot
epoch". No residual conflation — the snapshot's epoch claim is not
mapped onto a WS-generation bound anywhere in the file.

**2. Foreign-precheck errno — consistent.**
Line 49 CAN column: "a foreign-policy precheck may still return
another errno first." Matches D068 (`0x…dd84 cbnz w21 → return`;
nonzero precheck is an immediate return with the hook's errno) and
R014 producer #1 (`mac_proc_check_mac_syscall` precheck). The claim
correctly bounds the absent-guarantee to queries that *reach dispatch*.

**3. Hook-return-#103 caveat — consistent.**
Line 49 CANNOT column: "whether a hook returned `#103` (formally
open)". Matches D068's explicit "a hook CAN produce ENOPOLICY —
formally open" and R014 producers #1/#3 (precheck or matched
`mpo_policy_syscall` returning 103). Historic-producer identification
correctly retained as unprovable.

**4. Unaudited-caller claim — consistent.**
Lines 54–58 scope the negative result to "audited project sources and
SkyLight imports", declare Ventura launchd/tccd and other chroot
system images outside audit scope, and downgrade #103 observations to
"consistent with absent effective registration, not proof of it".
Matches D060's empirical posture exactly.

## Answer to the review question

Does absent prove all future failures are ENOPOLICY? — the text now
correctly says *no*: only queries reaching dispatch are guaranteed
the `0x865de80` no-match producer; precheck vetoes may return other
errnos. And historic `#103` branch identification is correctly kept
unprovable (hook-return-103 formally open).

## Verified supporting claims (re-checked, not just applied)

- "written only by `proc_set_responsible_pid`" — full-tree grep
  confirms the only assignment sites are `kern_proc.c:4703` and the
  fork-inheritance copy `kern_fork.c:1096`; all other
  `p_responsible_pid` references (`uipc_socket.c:716–724,8119–8123`,
  `necp.c:7246–7247`, `kern_exit.c:698–699`) are reads. Claim holds.
- `ops+0x388`/`+0x3a8` offsets match D068-verified slots.
- "tccd replies are already observed empty" matches D060/R003.
- No `PROC_PIDRESPONSIBLE` in `proc_info.h` — unimplementable-procinfo
  conclusion stands.
- Inadmissible D069 run correctly *not* cited as evidence anywhere in
  the edited text.

## One residual overstatement (minor, recommended edit)

Line 50, hypothetical procinfo row: the field read could prove
"**whether launchd/ancestors ever established responsibility for
target processes**". A bare `p_responsible_pid` value does not prove
provenance — the field is fork-inherited (`kern_fork.c:1096`), so a
recorded value may be inherited rather than established for that
process, and the field alone does not identify which ancestor called
the setter. Suggested wording: "the recorded responsible-pid value —
consistent with (not proof of) launchd/ancestor registration;
provenance unverifiable from the field alone". Low severity: the row
describes a non-implementable primitive, so this cannot affect the
executable scope.

## Bottom line

All four required edits verified exact against D060/D068/D069/R014.
One minor residual overstatement in a hypothetical (non-scope) row —
recommended wording fix, not blocking. Grant mechanism, hash binding
(`8bb90399…`), scope exclusions, and NOT APPROVED status intact.

(handover to codex now)
