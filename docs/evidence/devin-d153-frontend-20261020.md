# D153 — final source-accuracy audit of D152 contracts + acceptance mapping

Frozen inputs: `devin-152-backend-task.md`, `analysis/d141-svctime-
candidate/` (D143-repaired), D145. Line-specific verdicts; no edits, no
tests run.

## (1) `rec.seq` semantics — accurate with one conflation + one overclaim edge

- Allocation order confirmed: `.seq = fetch_add(&g_seq)` at emit build
  (`:127`) precedes the push's head claim — "allocated before the ring
  claim" is literal fact.
- "seq may be non-monotonic across producers" — correct (preempted
  producer inverts alloc vs claim).
- **Conflation to fix**: the record field `seq` (attempt counter
  `g_seq`) and the *slot* field `seq` (claim index+1, `:18,:117`) are
  different counters sharing a name. The D1 line should say
  "`record.seq` — the slot's internal seq is claim-order bookkeeping
  never exposed to the decoder" to prevent a decoder author reading
  slot semantics into the field.
- **Overclaim edge**: "gaps denote dropped attempts" — also covers a
  producer that allocates seq then dies before `d141_push` runs:
  seq consumed, no record, `dropped` NOT incremented. Precise wording:
  "seq gaps denote records that never reached the ring (bounded-drop or
  dead-producer attempts) — never committed-record loss."

## (2) D9 emit-path enumeration — substantively right, two elisions

Actual path (`:121-138` + push `:100-118`): armed acquire → errno save
→ seq fetch_add → clock_gettime → pthread_threadid_np → 2 relaxed loads
(pid, gen) → head acquire → bounded loop {tail acquire + weak CAS} →
optional test-hook → 64B struct copy → release seq commit → emitted
fetch_add → errno restore.

- Elided in the D9 line: the `g_head` acquire load + `g_tail` acquire
  load inside the claim loop, and the `g_dropped` fetch_add on the
  refused path. Suggested wording: "… bounded claim loop (head
  acquire-load, tail acquire-load per iteration, weak CAS), fixed
  64-byte structure copy, release commit, one counter increment (+
  `dropped` on the refused path) …".
- `s->rec = *r` is a struct copy — may compile to a `memcpy` call or
  inlined stores; "fixed 64-byte structure copy" is the accurate phrase
  (both forms nonblocking).
- clock_gettime/pthread_threadid_np are nonblocking; the
  "no blocking call" claim holds. TEST_HOOKS call exists only in test
  builds — worth noting so production reads don't audit a hook that
  isn't compiled in.

## (3) D8 wording — correctly scoped, no overclaim

"Exclusive with `MACWS_MACH_MSG_ALL`; enforcement is integration-layer
responsibility; prototype flags the obligation" — accurate: the
umbrella machinery lives in `mac_hooks.m`, absent from the candidate,
and the packet claims no implemented enforcement. PASS as written.

## (4) D10 vs no-production-integration — compatible, clarify one scope edge

A debug-only nested-record counter inside the candidate is fully within
`analysis/` scope. The stronger clause "nothing on the path may be
reachable from an instrumented boundary" cannot be *demonstrated* in
the candidate — it is an integration constraint. Suggested wording:
"in-candidate: emit/push/drain never call mach* or any wrapped
function — debug builds assert a reentry counter == 0. Integration
constraint: the same property must hold inside production hooks."

## (5) Single-consumer lease — stable, but state the permanence bound

- `pthread_threadid_np` ids are stable per thread and unique for the
  process lifetime — the tid lease is sound; header :17-18 already
  documents process-lifetime ownership.
- **Bound to state explicitly**: the lease is never released — if the
  consumer thread dies, no other tid can ever drain (records
  accumulate/drop; bounded but permanent). Acceptable for a diagnostic
  — say so.
- **Same-tid reentry unaddressed**: a signal-context `d141_drain` on
  the owner tid passes `owner == tid` and interleaves with the
  suspended drain. Add contract line "no drain from signal/async
  context" or a reentry counter.

## (6) R1–R8 coverage vs D145 mandatory rows

Complete except:

| D145 row | coverage | fix |
|---|---|---|
| A3 (slot-write only by claim owner) | implicit only | add one contract/assert line to I6 |
| D5 same-tid reentry | not covered | §(5) contract line or counter |
| D7 per-leg errno (mach legs) | structural only (emit's envelope covers; untested per-leg) | add `errno` unchanged checks inside the I5 mach-leg tests (cheap) |
| global-rejection-rule #5 (conservation blind to destroyed commits) | covered by R2/R3 per-record payload validation | OK |
| A2 stall bound | R2 drop assertion covers | OK |

All other D145 rows map: A1/A2→R2, B1→R2, B2/B3→R3(+R1), C1→R8(T6/T14)
+R1, C2→R1, C3→R6, D1→R6(see §1 fixes), D2→R3, D3→R4, D4/D6→R5,
D8/D9/D10→R6, hygiene→R7, regression→R8.

## Corrections summary for a D152-bis

1. D1: distinguish record.seq (attempt id) from slot seq (claim idx+1);
   reword gaps as "never-reached-ring", not just "dropped".
2. D9: enumerate head/tail loads, drop-path `g_dropped`, struct-copy
   phrasing, TEST_HOOKS-only call.
3. D5/lease: state permanence bound + no-signal-context-drain rule.
4. D10: split in-candidate assert vs integration constraint.
5. Add A3 contract line; add per-leg errno checks to I5 tests.
