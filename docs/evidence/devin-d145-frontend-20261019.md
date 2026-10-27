# D145 — reviewer acceptance checklist for the D143 repair of D141

Frozen inputs: D144 report (`devin-d144-frontend-20261019.md`), current
D141 candidate. Apply after backend's formal D143 handover. Every row is
PASS/FAIL with required evidence — a row without its evidence is FAIL.

Sanitizer-clean is necessary but NEVER sufficient: TSan/ASan/UBSan cannot
prove an adversarial interleaving was exercised. Rows marked ★ require a
deterministic test, not a race-probability test (no `sleep`-based windows).

## Gate A — slot reclamation (stop-at-hole)

| # | Requirement | PASS evidence | FAIL conditions |
|---|---|---|---|
| A1 | `g_tail` advances only past a contiguous committed prefix | Diff: drain loop `break`s (not `continue`s) on first `seq != tail+i+1`; `g_tail = tail + committed_len`. `g_skipped` either removed or kept as an always-zero invariant with an assert | Any path advancing tail across an unobserved commit |
| A2 | Wedged-producer bound documented | Test: producer parked pre-commit, N≥CAP further emits → `dropped` counts all, drain returns prefix; README states stall bound + recovery | Stall bound unstated, or drops not counted |
| A3 | No write to `c%CAP` without holding outstanding claim `c` | Code-review note: only the head-CAS winner writes `s->rec`/`s->seq`; with A1, `tail>c` ⇒ commit `c+1` observed ⇒ `c+CAP` unclaimable while `c` live | Any producer path writing a slot it did not just claim |

## Gate B — adversarial interleaving, deterministic

| # | Requirement | PASS evidence | FAIL conditions |
|---|---|---|---|
| B1 ★ | Parked-producer wrap test exists | A test-only injection point (barrier/hook) halts a producer between head-CAS and seq-commit. Deterministic script: park P at `c` → drain (prefix only, no skip) → wrap producer Q attempts `c+CAP` (blocked→drop, or waits) → resume P → P commits; Q's record intact or Q correctly refused | Sleep/timing windows; hook absent so the interleaving is unreachable |
| B2 ★ | Concurrent drain+produce churn | Drain thread runs WHILE ≥2 producers emit (≥10⁴ records or ≥5 s): every drained slot has `seq == expected` and payload matches a per-claim derivable pattern (no torn fields); conservation `committed == drained + in-ring` holds under overlap | Producers joined before draining (the T7 defect); pattern check absent |
| B3 | Senders can't hide behind sanitizers | Review note explicitly names which deterministic test exercises each D144 interleaving | "TSan clean" offered as race-freedom evidence |

## Gate C — publication & gating

| # | Requirement | PASS evidence | FAIL conditions |
|---|---|---|---|
| C1 | Gate decides once | Test: second `gate_init` returns latched value; post-init sentinel creation cannot arm | Any re-decision path |
| C2 | Generation atomicity | Either single-atomic `gen` with `.pid = gen>>32` (g_pid deleted), or enforced sequential-init contract + racing-set test showing no torn (pid,gen) pair | Two relaxed stores guarded only by check-then-set |
| C3 | Armed records never precede generation under the declared order | Contract line + test: records emitted between arm and set_generation either impossible or carry gen=0 flagged | Undocumented window |

## Gate D — record semantics

| # | Requirement | PASS evidence | FAIL conditions |
|---|---|---|---|
| D1 | Ordering contract explicit | Header/README: drain order = claim order; `rec.seq` = attempt id, gaps = dropped attempts; pairer must buffer reorder | Decoder treating seq-gap as committed-loss or flagging gap-then-fill |
| D2 | Unique payload/sequence validation | Test fills each record's fields from a claim-derived pattern; drain verifies per-record consistency. T7-style `seen[i]=1` vacuity is absent | Self-asserting loops; uniqueness never checked |
| D3 | PASS_EXIT carries post-call data | `d141_wrap_pass` signature changed (e.g. `aux_fn(ret)` or post-fn read); test shows exit aux = value only knowable after `fn` | Pre-call-fixed aux retained while header claims count support |
| D4 | Combined-trap leg join | Contract: CALL leg = send msgid, RET leg = recv msgid (0 on `kr!=SUCCESS`); join = same-tid adjacent CALL→RET. Test uses X≠Y ids and a TIMED_OUT ret excluded from pairing | Same-id model (T8's defect); timeout counted as arrival |
| D5 | Single-consumer drain enforced | Owner flag/CAS serializes drain, or contract + test proving second drain refused | Concurrent drains both advance tail |
| D6 | Nested mach legs | CALL/RET inside a CALL..RET window pair stack-wise or carry depth in aux; test covers | Flat pairing that mispairs under interposition |
| D7 | errno + return verbatim | Existing T3 retained + post-call-leg emit also covered | Emit touching errno unprotected on either leg |
| D8 | Umbrella-trace mutual exclusion | Contract line naming where `MACWS_MACH_MSG_ALL` write-path is suppressed during an armed window | Unaddressed (may live at integration layer but must be stated) |
| D9 | No blocking / no write(2) in emit | Audit note: emit→push path = CAS + memcpy + release store only | Any call that may block (lock, write, alloc) inside the measured window |
| D10 ★ | Record-path recursion impossible | Emit never enters a hooked function; optional nested-record assert counter == 0 under test | `mach_msg` reachable from the record path |

## Global rejection rules — candidate is REJECTED if any hold, even with all sanitizer tests green

1. No deterministic preemption hook exists → B1 unreachable → reject.
2. Any retain of "skip uncommitted + advance tail" → reject (P0 unclosed).
3. All produce/drain tests serialized (join-then-drain) → B2 unmet → reject.
4. Any vacuous assertion (self-written flag, tautology) → that row fails.
5. Conservation test that cannot detect a destroyed committed record (e.g. only `emitted+dropped==N`) → does not count as reclamation evidence.
6. Gen/pid tear possible under the declared init order → reject unless single-atomic or contract-enforced.
7. Missing contract lines: ordering semantics (D1), combined-trap legs (D4), trace mutual exclusion (D8), recursion bound (D10) → reject.
8. "No race found" claimed without the specific D144 interleaving being exercised → evidence requirement unmet → reject.

## Reviewer procedure

1. Diff the candidate against frozen D141; check A-gate structural rules first.
2. Run the full test binary; record checks/failures.
3. For each ★ row, verify the test is deterministic (barrier/injection, not timing).
4. Verify each contract line exists in header/README verbatim.
5. Apply global rejection rules before accepting — sanitizer green is a floor, not a ceiling.
