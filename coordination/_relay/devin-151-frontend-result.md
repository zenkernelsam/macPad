# D151 frontend result — 2026-10-20

Source-verified Mach wrapper contract + independent CAP arithmetic.
Report: `docs/evidence/devin-d151-frontend-20261020.md`.

## Key findings

- **Per-wrapper valid fields mapped** (file+line): all three wrappers
  carry umbrella-gated send-precall + recv-postcall legs — symmetric
  positions exist. Differences that change the contract: `mach_msg`
  shares one buffer (post-call `msgh_id` is the RECEIVED id; send id
  must be snapshotted pre-call — wrapper already does);
  `mach_msg_overwrite` recv id lives in `receive_buffer` (:13510);
  `mach_msg2` needs register unpacking per the documented ABI
  (:13544-13549, xnu-cited).
- **Corrected RET-leg rule**: msgid = recv id iff `kr==SUCCESS &&
  RCV`, else 0 — replaces my D150 "send id on send-only" (redundant).
  TOO_LARGE validity beyond `msgh_size` is an unverified unknown →
  conservative 0 + optional aux flag.
- **LIFO downgraded**: mach calls on one tid are strictly sequential
  — no in-window nesting is possible (kernel trap; record path can't
  recurse per D10). The real interleaving source is async reentrancy
  (signal handler on same tid) — same-tid nearest-unmatched rule still
  required but re-justified; the nested-legs test stands as an
  interleave-order test.
- **Timing legs must not inherit the 40000–41000 debug filter**
  (:13516/:13590) — that filter is trace-only.

## CAP arithmetic (independent replay of `c−tail ≥ CAP → drop`)

`emitted==511, dropped==1, drained==511, attempts==512` — conservation
holds; claim 511 is exactly the refused `c+CAP`; `uncommitted==1`
cumulative; mid-drain `==0`. D149's `emitted==CAP` superseded; D150's
correction was right.

## Unknowns listed for D150b

TOO_LARGE header validity (needs xnu cite); pass handled-count field
existence (stream-derived count doesn't need it); real wrapper leg
calls are integration scope; platform constants read from headers at
integration, not hardcoded.

No source/test edits, no builds, no device, no backend contact.
Milestone reminder: commit only after reviewer-accepted implementation
milestone — read-only work needs none.

(handover to codex now)
