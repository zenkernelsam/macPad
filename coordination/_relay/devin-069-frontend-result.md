# D069 frontend result — snapshot acceptance contract + interpretation bounds

Local read-only. Backend owns the live KRW read — not contacted; their
result had not landed at write time, so per task this delivers the
acceptance contract + missing-input checklist instead of a result
review. Evidence: `docs/evidence/devin-d069-policy-snapshot-
independent-review-20261019.md`.

## D068 matrix audit — survives with two sharpenings

- Snapshot and observer answer **different layers**: kernel
  registration vs sender-side check outcome; neither substitutes.
- Any observed `#103` keeps three candidate producers (no-match
  default @ `0x865de80` verified, foreign-policy precheck veto at
  ops+0x388, dispatched handler). The snapshot resolves only whether
  a Quarantine-named handler *can exist*.

## Acceptance criteria (each row fail-closed)

A1 kernel identity (live kernbase via trusted primitive, build string
`8792.82.2~1/T8103`, boot-kc SHA already `78959dd9`-closed); A2 slide
proof + sanity read before trusting entries; A3 header double-read
tear check + `numloaded≤max≤0x400`; A4 index≤0x40 / name≤0x80
printable-NUL caps; A5 PAC slots — low56 NULL-test only, no decode
needed for presence; A6 per-entry name + `ops+0x388`/`+0x3a8`
**presence flags only, never invocation**; A7 WS/tccd pid+lstart+state
seq before-after; A8 exits+hashes+cleanup.

## Interpretation contract

- **Absent Quarantine** ⇒ dispatch arm closed for that boot epoch
  (`mac_late=1` ⇒ per-epoch stability). Does NOT prove which arm
  produced historical 103s, what a handler would return, permanence,
  or other builds; userspace legs untouched.
- **Present Quarantine+hook** ⇒ dispatch arm possible; absence
  hypothesis falsified. Does NOT prove the historical call dispatched
  (precheck runs first), that `0xb6` is a valid selector there, or
  return values.
- Point-in-time read either way — per-boot-epoch, not permanent.

## Sensitive-data hygiene

Raw kernel addresses are KASLR-bearing — saved evidence must carry
slide-relative offsets / presence flags only, no raw VAs, no tokens,
no unrelated reads; pid/lstart/state-seq are the legitimate witnesses.

## Missing-input checklist owed by backend

Identity+slide lines; double-read header transcript; per-index walk
log (pointer checks, names, two ops flags, caps honored); before/after
generation witness; helper hash + cleanup proof; sanitization applied.
If slide/list-pointer can't be independently verified the contract is
STOP-without-read — a missing-input report is the correct terminal.

(handover to codex now)
