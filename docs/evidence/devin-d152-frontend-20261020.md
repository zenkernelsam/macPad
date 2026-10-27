# D152 — consolidated D150b backend execution packet

Deliverable: `coordination/_relay/devin-152-backend-task.md` (final,
self-contained; supersedes D149 §task-body where they differ).
Sources consolidated: D145 checklist, D149 schedules, D150 corrections,
D151 source audit, frozen D143 candidate.

## Contradiction resolutions (explicit)

| Topic | Resolved as | Why |
|---|---|---|
| CAP totals | `emitted==511, dropped==1, drained==511, attempts==512` | D150 replay against the exact `c−tail ≥ CAP` rule; D149's `emitted==CAP` superseded |
| Gate-spin observation | `g_gate_spun` counter (preferred) or cas-fail hook — REQUIRED | D150 defect-1: without it the retry path can't be provably exercised |
| Live drain/producer overlap | `drain_started` latch + parked-producer prefix proof | D150 defect-3: start-before-go alone is unprovable |
| PASS_EXIT handled-count | contract route preferred (stream-derived count = dispatch edges inside pass bracket); API route as stated alternative | D150 item-4: derivation needs no new API; either closes D3 — backend must declare which |
| RET-leg msgid rule | recv id iff `SUCCESS && RCV`, else 0 | D151 §2: send-id-on-send-only redundant; matches what each wrapper can trust |
| TOO_LARGE | conservative msgid=0; only `msgh_size` defined | D151 §7: no xnu citation in-tree |
| Same-tid pairing | nearest unmatched CALL — justified for async reentrancy (signal-handler mach calls), NOT ordinary trap nesting (impossible) | D151 §5 |
| Send-field capture | mandatory pre-call in mach_msg (shared buffer); msg2 unpacks registers | D151 §§1,3 |
| T12 stale-flag claim | refuted — T12 resets both atomics at :278 | D148 |
| T13 flakiness | real defect — losing-CAS spin returns -1 on UNINIT window | D148; fixed by I1 |

## Packet structure (for the record)

I1 gate-retry determinism (re-CAS + spin observability + gen-init hook
+ T15 schedule + T13 fix); I2 CAP-boundary refusal (exact totals);
I3 deterministic overlap churn; I4 pass-aux route choice; I5 Mach-leg
contract per D151; I6 verbatim contract lines D1/C3/D8/D9/D10; I7 test
hygiene. Acceptance rows R1–R8; forbidden scope restated; commit only
after reviewer acceptance of the implementation milestone.

## Frontend state after this packet

All D145 gates have a mapped acceptance row; the earlier review's
confirmed rows are preserved; its one wrong claim is annotated. The
candidate remains isolated PENDING.
