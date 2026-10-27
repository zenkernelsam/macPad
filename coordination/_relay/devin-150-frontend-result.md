# D150 frontend result — 2026-10-20

Self-audit of the D149 packet complete; three real defects found and
corrected. Report: `docs/evidence/devin-d150-frontend-20261020.md`.

## Defects in my own packet

1. **Gate-retry schedule not deterministic as written** — only
   `d141_hook_gen_init` was specified; the losing-CAS spin is
   unobservable, so "gate is spinning" can't be proven before release.
   Fix: `g_gate_spun` counter (preferred) or a cas-fail hook; then the
   release barrier is provable.
2. **CAP-boundary totals wrong** — replayed the real bound rule: claim
   511 IS the refused `c+CAP` attempt (correct), but expected totals
   must be `emitted==511` (not CAP), `drained==511`, `uncommitted==1`
   cumulative (it never returns to 0); added mid-drain `==0` assertion.
3. **"drain before go" doesn't prove overlap** — scheduler can delay
   the consumer entirely. Fix: `drain_started` latch on producers'
   wait + deterministic overlap proof via parked-producer prefix
   drain while P2 emits.

## Verified-correct parts

c+CAP refusal math (255+256=511 refused at tail=255); item-4 API
feasibility (plus a cheaper contract-route alternative: handled-count
derivable by counting dispatch edges inside the pass bracket — no API
needed); stop-at-hole impossibility assertions.

## New flags raised

Mach legs: send-only RET msgid rule needed; `MACH_RCV_TOO_LARGE`
header-valid decision needed; mach_msg2_internal leg coverage must be
verified across all three real wrappers.

## Deliverables

Corrected acceptance table + corrected `D150b` backend task body
(replaces the D149 draft where they differ). No candidate/test/source
edits, no tests run, no device, no backend contact.

Milestone reminder: commit only after reviewer-accepted implementation
milestone; read-only work needs none.

(handover to codex now)
