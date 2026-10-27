# D148 — independent verification of the D143 repair review vs D145 checklist

Frozen inputs: `analysis/d141-svctime-candidate/` (D143-repaired),
`coordination/reviews/D143-D145-20261020.md`, D144/D145. No tests run,
no edits, no device.

## Review verdict replication

CHANGES_REQUIRED / offline PENDING — **replicated**. Reproduction claims
(129/130 checks, ASan+TSan clean) taken as transcription; not rerun per
task scope.

## Row-by-row verification vs source

| Gate | Review | My check | Verdict |
|---|---|---|---|
| A1 | PASS — break at first uncommitted, tail += committed | `d141_svctime.c:182-194` — `break` on seq mismatch, `g_tail = tail + i` | **Confirmed** |
| A2 | PARTIAL/FAIL — bound documented, no parked+flood test | README :33-35 states wedge bound; no test emits >CAP while parked (T12 tops out at emitted==CAP) | **Confirmed** |
| A3 | PASS by review | only head-CAS winner writes `s->rec`/`s->seq` (:115-117) | **Confirmed** |
| B1 | FAIL — no same-slot `c+CAP` attempt while parked | T9 emits next index only (:199); T12 wraps to slot 0 (consumed), never `c+CAP` (:296) — the dangerous claim is unreachable by design, but nothing *asserts* that refusal dynamically | **Confirmed** |
| B2 | FAIL — no drain overlapping ≥2 live producers | T7 joins before draining (:361); T9/T12 park a single producer | **Confirmed** |
| B3 | PARTIAL | names present; exact D144 interleavings uncovered | **Confirmed** |
| C1 | PASS | state CAS enforces once-only decision; T6/T14 cover | **Confirmed** |
| C2 | PARTIAL — torn pair excluded; tautological else-branch | `emit_seen_gen == 0 || emit_seen_gen == 0` at :349-350 — verbatim tautology, asserts nothing | **Confirmed** |
| C3 | PARTIAL — ordering rule unstated | release-chain is correct (gen stores → UNINIT release → gate acquire → ARMED release → emit acquire), but the armed-record ordering rule isn't written anywhere | **Confirmed** |
| D1 | FAIL — seq semantics unstated | README :23-29 describes prefix drain only; no "seq = attempt id / gaps = drops / pairer must buffer reorder" | **Confirmed** |
| D2 | PARTIAL/FAIL | T11 checks msgid uniqueness+range (:264-268) — not a per-claim full-field pattern | **Confirmed** |
| D3 | FAIL — pre-call aux retained | `d141_wrap_pass(exit_aux,…)` unchanged (:146); README :81 still claims "aux carries handled-count" — **the contract asserts a capability the API cannot deliver** | **Confirmed** |
| D4 | FAIL | T8 sends same msgid on both legs (:375-376); no distinct send/recv-id join, no timeout exclusion | **Confirmed** |
| D5 | PASS w/ caveat | `g_consumer` tid lease :167-172; same-thread reentry unhandled | **Confirmed** |
| D6 | FAIL | T4 nests pass brackets only; no nested MACH legs | **Confirmed** |
| D7 | PARTIAL/FAIL | errno envelope exists in `emit` (:124,137) covering both mach legs structurally — but no per-leg test | **Confirmed** |
| D8 | FAIL | no MACWS_MACH_MSG_ALL mutual-exclusion line anywhere | **Confirmed** |
| D9 | PARTIAL | push = CAS+memcpy+release only (:100-118); no full-emit-path audit note (clock_gettime/threadid_np inside measured window) | **Confirmed** |
| D10 | FAIL | no recursion-bound contract or assert | **Confirmed** |

## Corrections to the review

1. **T12 defect claim is REFUTED.** Review says "T12 resets `hook_gate`
   but not `hook_entered`" — `d141_svctime_test.c:278` resets *both*.
   `d141_reset_for_test` also clears `d141_hook_post_claim`. The claimed
   stale-flag interleaving does not exist; row stands on B1 grounds only.
2. **Review missed a real defect — T13 is nondeterministic and can
   deterministically FAIL.** `gate_init` losing the CAS to a concurrent
   `set_generation` enters the spin loop (:88-93) which only exits on
   ARMED/DISARMED — but `set_generation` rolls back to **UNINIT** (:72),
   which the loop never matches and never re-CASes → the gate spins 10M
   iterations and returns **-1**. T13's `CHECK(gate_rc == 1)` (:336)
   then fails whenever the gate's first CAS lands inside gen's
   INITIALIZING window — a coin-flip the test's own `go` barrier can
   produce. Same underlying wart: a first `gate_init` can return -1
   merely for observing another thread's setup — callers per the
   contract call once, so -1 leaves the process ungated unless retried.
   Either the spin must re-attempt the CAS on UNINIT, or T13 must accept
   gate_rc ∈ {1, -1} and retry — currently the test is flaky-by-design.

## Minimal isolated follow-up scope (not implemented)

1. `gate_init`: re-CAS on UNINIT inside the bounded spin (or document
   that -1 demands caller retry) + T13 asserting `gate_rc ∈ {1,-1}` with
   a retry step.
2. Same-slot assertion test: park producer at claim `c`, drain, then
   drive head to `c+CAP-1` and assert the `c+CAP` claim is refused/
   dropped (proves the unreachable interleaving stays unreachable).
3. Multi-producer + concurrent-drain churn with per-claim payload
   patterns (B2/D2 joint).
4. `d141_wrap_pass` post-call aux path (or honest README downgrade of
   the handled-count claim).
5. Distinct send/recv msgid T8 variant + timeout-tag exclusion (D4);
   nested MACH legs (D6); contract lines for D1/C3/D8/D9/D10.

## Milestone note

Per task instruction: this task is read-only — nothing to commit.
Reminder for later: commit only after the reviewer accepts a concrete
change.
