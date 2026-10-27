# D149 — deterministic follow-up test/design packet for the D141/D143 candidate

Prepared from D145 checklist + D148 verification + frozen D143 sources.
Design/test specification only — no source or test edits.

## Item 1 — gate_init losing to set_generation, retrying after UNINIT

**Defect under test** (D148): a `gate_init` whose CAS loses to a
concurrent `set_generation`'s INITIALIZING spins on ARMED/DISARMED only,
never matches the UNINIT rollback, never re-CASes → returns -1 after
10M spins. First-call -1 leaves the process ungated if callers follow
the call-once contract.

**Design fix (preferred)**: inside the bounded spin, if `g_state` is
observed UNINIT, re-attempt the CAS into INITIALIZING. Fallback option:
define `-1` as "retry required" in the contract and require callers to
loop — less preferred because the call-once contract then has a hidden
obligation.

**Deterministic schedule**: requires a new test hook —
`d141_hook_gen_init` invoked inside `set_generation` *while holding
INITIALIZING* (after the CAS, before rollback). Barrier script:

1. arm-capable sentinel present; `go=0`.
2. Thread G calls `set_generation` → claims INITIALIZING → blocks in
   `d141_hook_gen_init`.
3. Thread T calls `gate_init` → its CAS sees INITIALIZING → enters spin.
4. Main waits until T is provably spinning (hook-observed CAS failure or
   a bounded join-poll), then releases G's hook.
5. G stores pid/gen, rolls back UNINIT.
6. **Expected**: T observes UNINIT, re-CASes, probes sentinel,
   publishes ARMED, returns 1. `gate_rc == 1`, `gen_rc == 0`.
7. Post-state: one emit carries `gen == (pid<<32)^uuid_lo`, `pid` —
   coherent pair (C2 retention).

**Invariant asserted**: gate always terminates with the final decision;
no -1 under this exact schedule. Also keep T14 (8 concurrent gates all
see ARMED).

## Item 2 — parked claim c + exact c+CAP refusal

**Key fact to encode**: under stop-at-hole, `tail` can never pass the
parked claim `c`, so a claim of `c+CAP` is *arithmetically refused by
the bound check* (`c+CAP − tail ≥ CAP` while `tail ≤ c`). The "attempt"
IS an emit that must drop — the schedule is possible; the impossible
schedule is a *successful* same-slot overwrite while parked (assert its
non-occurrence via the drop counter, not a data check).

**Deterministic schedule** (extends T12):

1. Emit `c = CAP−1` committed records (indices 0..254).
2. Arm `d141_hook_post_claim`; producer P claims index `c=255` → parks.
   Main waits `hook_entered`.
3. Drain → exactly 255 records, `g_uncommitted==1`, `tail==255`.
4. Emit claims 256..510 (255 emits — each lands on a consumed slot;
   `dropped==0`). Ring now: head=511, tail=255, outstanding=256=CAP.
5. Emit once more → **must drop** (`dropped==1`, `emitted` unchanged).
   This IS the refused `c+CAP` claim (256 ahead of tail 255).
6. Disarm hook, release gate → P commits index 255 (slot 255).
7. Drain → 256 records: P's `0xDEADBEEF` first, then the 255 followers,
   in claim order. `emitted == CAP`, `dropped == 1`, `uncommitted==1`.

**Asserted instead of impossible schedule**: no test can force P's late
commit to write a re-claimed slot — the invariant is that the bound
made re-claim impossible. Assert `dropped` incremented at exactly the
CAP+1'th outstanding attempt and the full ordered drain.

## Item 3 — ≥2 live producers overlapping drain, full-payload validation

**Schedule**: `go`-barrier. P producer threads (≥2, use 4) each emit K
records with `msgid = base_i + j` unique and `aux = msgid ^ 0x5A5A00`
(claim-derived pattern). Main thread (lease owner) runs
`d141_drain` in a loop CONCURRENTLY — starts before `go`, stops when
`drained + dropped == P*K`.

**Invariants**: every drained record — `magic` valid, `msgid` in-range
unique, `aux == msgid ^ 0x5A5A00` (full-field tear detection, not just
uniqueness), `tid` ∈ producer set, `gen` uniform. `drained == emitted`,
`emitted + dropped == P*K`. After quiesce `uncommitted` returns to 0.
Deterministic anchor: combine with the park hook — park P1 mid-commit,
P2 keeps emitting + drain runs → drain returns only prefix; release →
remaining records arrive complete. The overlap guarantee comes from
starting the drain loop before `go`.

## Item 4 — post-call PASS_EXIT aux semantics

**API fix (proposed, minimal)**: change semantics so the exit record's
aux is produced *after* `fn` returns:

```c
uintptr_t d141_wrap_pass_ex(
    uint64_t (*aux_after)(uintptr_t ret, void *ctx), void *ctx,
    uintptr_t (*fn)(void *), void *arg) {
    d141_emit(D141K_PASS_ENTER, 0, 0);
    uintptr_t r = fn(arg);
    d141_emit(D141K_PASS_EXIT, 0, aux_after ? aux_after(r, ctx) : 0);
    return r;
}
```

(or keep `d141_wrap_pass` and read post-call state from `arg` —
either is acceptable; what matters is aux is computed **after** `fn`.)

**Test**: `fn` increments a counter to 3 and returns it; `aux_after`
maps `ret` → that count. Assert PASS_EXIT record `aux == 3` — a value
unknowable pre-call. Assert PASS_ENTER unchanged (msgid=0, aux=0) and
return value verbatim. Remove/annotate the README line claiming
"aux carries handled-count" until the API delivers it.

## Item 5 — Mach leg semantics

**Contract text (required)**:

- `d141_mach_call(msgid, option)`: msgid = send-header id if
  `option & MACH_SEND_MSG`, else 0.
- `d141_mach_ret(msgid, kr)`: msgid = recv-header id only when
  `kr == KERN_SUCCESS`, else 0 (buffer content unspecified on failure).
- Pairing rule (decoder-side): a MACH_RET joins the most recent
  unmatched MACH_CALL on the same tid (stack discipline); combined
  send+rcv traps produce CALL(recv-id may differ) pairs where the send
  edge timestamp is the entry ts.
- Reply correlation: reply `msgid − 100` joins the request; that join
  is decoder work, the record only carries raw ids.

**Tests**: (a) CALL(0x74a7, send|rcv) + RET(0x750b, 0) → two records,
distinct ids, adjacent same-tid; (b) RET(0, RCV_TIMED_OUT) → record
with msgid=0, aux=kr, excluded from join; (c) nested
CALL(A)→CALL(B)→RET(B)→RET(A) → order proves stack pairing.

## Item 6 — required contract lines (verbatim-able)

- **D1**: "Drain order is claim order. `rec.seq` is an attempt
  identifier allocated before the ring claim: gaps denote dropped
  attempts, and seq may be non-monotonic across producers. Pairers must
  buffer reorder and never treat gap-then-fill as loss."
- **C3**: "Under the documented order (`set_generation` before
  `gate_init`), no record is emitted before generation is published;
  records emitted while disarmed/uninit are impossible. The reverse
  order yields armed records with zeroed pid/gen — forbidden by
  contract."
- **D8**: "An armed timing window requires `MACWS_MACH_MSG_ALL` off;
  the record path is exclusive with the umbrella write(2) trace.
  Enforcement lives at the integration layer; this prototype flags the
  obligation."
- **D9**: "emit→push executes exactly: armed check, seq fetch_add,
  clock_gettime(MONOTONIC), pthread_threadid_np, two relaxed loads,
  bounded CAS claim, fixed 64B memcpy, release store, counter increment,
  errno save/restore. No lock, write(2), allocation, or blocking call
  exists on this path."
- **D10**: "No function on the emit/push/drain path may be reachable
  from an instrumented boundary. Debug builds keep a nested-record
  counter asserted at 0."

## Impossible schedules under bounded-drop semantics

| Schedule | Why impossible | Assert instead |
|---|---|---|
| Producer overwrites a live parked slot | bound check refuses `c+CAP` while `tail ≤ c` | `dropped` counts the refused attempt (item 2) |
| Drain waits for a parked producer | drain is a bounded snapshot; must return prefix | prefix length + `uncommitted` count |
| Deterministic torn mid-memcpy read | no hook inside `s->rec = *r` | per-claim full-payload pattern (item 3) detects any occurrence |
| Open slot without TEST_HOOKS | only the hook reaches claim-without-commit | gate production runs on hook absence |
| seq-ordered drain | seq is allocated pre-claim | contract D1 + reorder-tolerant pairer test |

## Proposed next backend task body

> **D150 backend — repair D141 candidate per D149 packet.**
> Edit only `analysis/d141-svctime-candidate/` (+ its README/tests).
> Implement: (1) gate_init re-CAS on UNINIT inside the bounded spin (or
> documented -1-retry contract + caller loop); (2) T15 parked-claim
> CAP-boundary refusal test per D149 §item-2; (3) T16 overlapping
> multi-producer + concurrent drain with full-payload validation per
> §item-3; (4) post-call pass-exit aux per §item-4; (5) mach-leg
> distinct-id/timeout/nested tests per §item-5; (6) contract lines
> D1/C3/D8/D9/D10 verbatim per §item-6; (7) fix T13 flakiness
> (gate_rc∈{1,-1}+retry or deterministic hook); (8) fix T13's
> tautological else-branch. Add `d141_hook_gen_init` test hook for the
> item-1 schedule. Constraints: no production source, permits, package,
> device, sentinel, or GUI contact; keep D141 record format; document
> any API delta. Run existing tests + new; report check counts.
> Reviewer will apply `docs/evidence/devin-d149-frontend-20261020.md`
> acceptance rows before acceptance.

## Per-test acceptance checklist

| Test | Accept when |
|---|---|
| gate-retry (item 1) | deterministic schedule reproducible via `d141_hook_gen_init`; `gate_rc==1` every run; spin cannot return -1 on this path |
| CAP-boundary (item 2) | exact counts: `emitted==CAP`, `dropped==1`, drain order `[parked, followers…]` |
| overlap churn (item 3) | drain loop overlapped `go`-released producers; per-record `aux==f(msgid)` + magic + unique msgids; conservation exact |
| post-call aux (item 4) | PASS_EXIT.aux equals post-call-derived value; README claim/API consistent |
| mach legs (item 5) | distinct-id join, timeout exclusion, nested stack order all asserted |
| contract lines (item 6) | five sentences present verbatim-ish in README/header |
| T13 | non-flaky: either gate re-CAS or {1,-1}+retry asserted deterministically; tautology removed |
| regression | all retained tests pass; counts reported |

Milestone note: once a concrete backend milestone is reviewer-accepted,
Devin must commit that milestone. This planning task requires no commit.
