# D150b backend — isolated D141/D143 candidate repair per consolidated D145/D149–D151 packet

Scope: `analysis/d141-svctime-candidate/` only (`.c`, `.h`, `_test.c`,
`README.md`, `run_tests.sh`). **Forbidden**: production source, permit
tables, package/build/deploy, device access, sentinels, GUI, messaging
other agents. Keep the 64-byte `d141_record` format; document every API
delta. This is a consolidated, self-contained spec — where it differs
from earlier packets, this file wins.

Revision map: D154 incorporated D153 wording/coverage corrections
(seq disambiguation, A3, D5, D9 enumeration, D10 split, per-leg
errno). D156 corrected the I5 RET aux contract per D155 — `aux` =
`kr` verbatim; option bits remain on the paired CALL aux, recovered
via same-tid pairing; no API delta, no D9 packing work. D158 added
the D157 A2 saturation-flood step to T16 plus the symmetric
producer-stall bound in D5 (still seven contract lines). D159 fixed
T16 hook sequencing: the global `d141_hook_post_claim` must be
cleared after `hook_entered` and before any fill emit, else the test
deadlocks inside `pause_hook`.

## Implementation items

### I1 — gate retry determinism

- `d141_gate_init`: inside the bounded spin, when `g_state` is observed
  `UNINIT`, re-attempt the CAS into `INITIALIZING` (instead of spinning
  to -1). A first gate call may no longer fail permanently for merely
  observing another thread's `set_generation` window.
- Add observability for the losing-CAS path: counter `g_gate_spun`
  incremented once per spin entry (preferred, production-harmless) OR a
  `d141_hook_gate_cas_failed` test hook. Required — without it the
  deterministic schedule below cannot prove the retry path ran.
- Add test hook `d141_hook_gen_init`, invoked inside
  `d141_set_generation` while it holds `INITIALIZING` (after the CAS,
  before the pid/gen stores + UNINIT rollback). `D141_TEST_HOOKS` only.
- Deterministic T15: sentinel present; `go` barrier; thread G enters
  `set_generation` and parks inside `d141_hook_gen_init`; thread T
  calls `gate_init`, CAS-loses, enters spin (`gate_spun ≥ 1` observed);
  main releases G → rollback UNINIT → T re-CASes → ARMED.
  Assert `gate_rc == 1`, `gen_rc == 0`, then one emit carries the full
  `(pid<<32)^uuid_lo` gen + pid coherently.
- Fix T13: it must be non-flaky — with I1 either `gate_rc == 1`
  becomes deterministic, or assert `gate_rc ∈ {1,-1}` plus an explicit
  retry step. Also delete the tautological `emit_seen_gen == 0 ||
  emit_seen_gen == 0` else-branch and replace with a real check
  (gen refused ⇒ records carry zeroed pid/gen, or forbid per contract).

### I2 — CAP-boundary refusal + saturation flood (T16)

Exact schedule (rule `c − tail ≥ CAP → drop`, CAP=256). The hook is
GLOBAL — `d141_svctime.c:113` invokes `d141_hook_post_claim` after
every successful claim — so it must be disarmed once P is provably
inside, or every later fill emit would enter `pause_hook` and block
the test itself (refused attempts never reach the hook — the bound
check precedes the claim):

1. Emit 255 committed (claims 0..254, hook NULL).
2. Arm `d141_hook_post_claim = pause_hook`; producer P claims index
   255, enters the hook, sets `hook_entered`, spins on `hook_gate`.
   Exactly ONE claimant may emit while the hook is armed — any other
   emitter in this window would also park and corrupt the schedule.
3. Main waits `hook_entered`, then clears `d141_hook_post_claim =
   NULL`. P remains blocked inside its already-entered `pause_hook`
   call on `hook_gate`; the clear only prevents subsequent claimants
   from invoking it (T9/T12 pattern: `:185-203`, `:283-299`).
4. Drain → exactly 255 records, `g_uncommitted ≥ 1`, tail=255.
5. Emit 255 records → claims 256..510 all land (`c−tail = 1..255 <
   CAP`), `dropped == 0`, head=511, emitted=510 — hook is NULL, no
   fill emit can block.
6. Emit once → claim 511 = `255+256` = the refused `c+CAP` attempt:
   `dropped == 1`, emitted unchanged.
7. Saturation flood (D145 A2 — P still parked, tail=255, head=511):
   emit M more attempts (M = CAP = 256). Every attempt sees
   `511−255 = 256 ≥ CAP` → drop. Assert `dropped == 1 + M`,
   `emitted == 510`, `g_head == 511` (refusals never consume a claim
   index), and a second mid-park drain still returns 0. Correctness
   must survive N consecutive drops, not just the first.
8. Release `hook_gate = 1` → P exits `pause_hook`, writes `s->rec`,
   release-commits `s->seq`. `emitted == 511`.
9. Drain → 256 records, order `[DEADBEEF, 255 followers by claim]`;
   mid-window drain before release must return 0.

Acceptance totals: `emitted == 511`, `dropped == 1 + M`,
`drained == 511`, `attempts == 512 + M` (conservation
`emitted+dropped == attempts`), `uncommitted` cumulative ≥ 1 (never
returns to 0 — count is monotonic). Note the invariant being
asserted: same-slot reuse while parked is *arithmetically
impossible*; the refusal is the drop count, and the flood proves the
bound stays honest under sustained saturation.

### I3 — deterministic multi-producer + overlapping drain (T17)

- `drain_started` latch: consumer thread sets it before its loop;
  producer `go` waits on BOTH `drain_started` and `go`. This only proves
  the consumer was scheduled — the true-overlap proof is the next step.
- True-overlap proof: park P1 at its post-claim hook; run P2 emitting +
  consumer draining concurrently → drain returns prefix-only with
  `uncommitted > 0`, proving it observed live uncommitted state during
  active production. Release and continue churn.
- Churn phase: ≥2 producers (use 4) × K emits with
  `msgid = base_i + j` unique and `aux = msgid ^ 0x5A5A00`. Consumer
  drains until `drained + dropped == P*K`.
- Per-record validation: `magic` valid, `msgid` unique in range,
  `aux == msgid ^ 0x5A5A00`, `tid ∈ producers`, `gen` uniform.
  `drained == emitted`, `emitted + dropped == P*K`.

### I4 — PASS_EXIT handled-count — choose ONE route, state it

- **Contract route (preferred)**: no API change. Contract line: "the
  per-pass handled count is derivable by counting DISPATCH/HANDLER/
  IMPL edges between PASS_ENTER and PASS_EXIT on the same tid;
  PASS_EXIT.aux may remain 0" + a decoder-side note. Update the
  misleading README claim "aux carries handled-count" either to match
  the derivation rule or to be deleted.
- **API route (alternative)**: `d141_wrap_pass_ex(aux_after, ctx, fn,
  arg)` emitting PASS_EXIT with `aux_after(ret, ctx)` computed after
  `fn` returns; test aux == a value unknowable pre-call.
- Either way the current pre-call-fixed `exit_aux` parameter must not
  be retained while documented as carrying post-call data.

### I5 — Mach legs per D151 source audit

Contract lines (write into header/README):
- CALL leg: `msgid` = send-header id when `option & MACH_SEND_MSG`,
  else 0. For `mach_msg2` the send id comes from the packed-register
  ABI (id = hi32(arg4), bits = lo32(arg2)). `aux` = `option` verbatim.
  Send fields MUST be captured pre-call — `mach_msg` shares one
  buffer, so post-call `message->msgh_id` is the received id.
- RET leg: `msgid` = recv-header id iff `kr == MACH_MSG_SUCCESS &&
  option & MACH_RCV_MSG` (read `message` for mach_msg — overwritten;
  `receive_buffer` for overwrite; `data` for msg2), else 0. `aux` =
  `kr` verbatim. Option bits are NOT carried on RET — they stay
  verbatim on the paired CALL's aux and are recovered by the
  same-tid pairing rule (D155). No packing; the existing
  `d141_mach_ret(msgid, kr)` signature stands — zero API delta.
- `MACH_RCV_TOO_LARGE`: treat msgid as 0 (conservative; only
  `msgh_size` is defined). Optional aux flag for truncated-recv.
  Do NOT claim header validity without an xnu citation.
- Pairing: nearest unmatched same-tid CALL — required for async
  reentrancy (signal-handler mach calls), NOT ordinary trap nesting
  (impossible: kernel trap, no user code in-window).
- Reply correlation `id − 100` is decoder-side; records carry raw ids.
- Timing legs must not inherit the trace-only 40000–41000 filter.
- Verify all three real wrappers can host both legs (integration
  scope note — no integration edits in this task).

Tests: distinct-id join `CALL(0x74a7)→RET(0x750b)`; timeout
`RET(0, TIMED_OUT)` excluded; async-interleave order
`CALL(A)CALL(B)RET(B)RET(A)` pairing; aux preservation — assert CALL
`aux == option` verbatim AND RET `aux == kr` verbatim; per-leg errno —
assert errno unchanged across `d141_mach_call` AND `d141_mach_ret`
(D7 per-leg coverage, not just the shared emit envelope).

### I6 — contract lines (add verbatim-ish to README/header)

- D1: drain order = claim order. `record.seq` is an *attempt
  identifier* allocated before the ring claim — distinct from the
  slot-internal seq (claim index+1), which is ring bookkeeping never
  exposed to the decoder. `record.seq` gaps denote records that never
  reached the ring (bounded-drop or dead-mid-emit attempts) — never
  committed-record loss — and seq may be non-monotonic across
  producers; pairers must buffer reorder.
- C3: under documented order (`set_generation` before `gate_init`), no
  record emits before generation publishes; reverse order yields
  zeroed pid/gen — forbidden.
- A3 (per D153): only the head-CAS winner may write a slot — `s->rec`
  /`s->seq` stores belong solely to the thread holding that claim.
  Restate as an invariant line.
- D5 (per D153): the consumer lease is process-lifetime and never
  released — a dead consumer thread permanently disables draining
  (records accumulate/drop; bounded, by design). No `d141_drain` from
  signal or other async context even on the owner tid — same-tid
  reentry is unserialized. Symmetric producer bound (D157): a
  producer that dies or stalls holding a claim permanently
  head-of-line-blocks the ring at the CAP bound — every subsequent
  emit drops counted; the ring never corrupts and `g_dropped` is the
  honest record of the stall.
- D8: armed timing window is exclusive with the
  `MACWS_MACH_MSG_ALL` umbrella trace; enforcement is integration-layer
  responsibility; prototype flags the obligation.
- D9: emit→push = armed acquire check, errno save, `g_seq` fetch_add,
  clock_gettime(MONOTONIC), pthread_threadid_np, two relaxed loads
  (pid, gen), `g_head` acquire load, bounded claim loop (`g_tail`
  acquire load + weak CAS per iteration), optional `D141_TEST_HOOKS`
  callback (test builds only), fixed 64-byte structure copy (compiler
  inline or memcpy — either is nonblocking), release seq commit, one
  `g_emitted` increment (plus `g_dropped` on the refused path), errno
  restore — no lock, write(2), allocation, or blocking call.
- D10: in-candidate — emit/push/drain never call mach* or any wrapped
  function; debug builds assert a reentry/nested-record counter == 0.
  Integration constraint — the same property must hold inside the
  production hook path.

### I7 — test hygiene

- T7/T11 retained; keep `seen[]` semantics real (T11's unique-msgid
  conservation already does this — retain).
- No vacuous assertions anywhere; no sleep-based windows; all
  adversarial cases must be barrier/hook-driven.
- Retain T9/T10/T12/T14.

## Acceptance rows (reviewer applies)

| # | row | accept when |
|---|---|---|
| R1 | gate retry | gate_spun ≥1 observed inside deterministic T15; `gate_rc==1`; gen+pid coherent |
| R2 | CAP boundary + flood | emitted==511, dropped==1+M (M=CAP attempts all refused while parked), drained==511, mid-drain==0, ordered final drain, conservation attempts==512+M |
| R3 | overlap churn | drain_started latch + parked-prefix overlap proof; per-record aux==f(msgid)+magic+unique; conservation exact |
| R4 | pass aux | chosen route stated; either aux==post-call value (API) or contract line + decoder rule (contract); README reconciled |
| R5 | mach legs | contract lines per I5 (RET aux = `kr` verbatim; option on paired CALL aux); distinct-id join, timeout exclusion, async-interleave pairing, aux-preservation and per-leg errno tests pass |
| R6 | contract | seven lines present: D1/C3/A3/D5/D8/D9/D10 (D1 with seq disambiguation, D9 with full enumeration, D10 split-scope) |
| R7 | hygiene | no tautologies, no sleeps, hooks barrier-driven |
| R8 | regression | all retained tests pass; check counts reported (run_tests.sh) |

## Report requirements

Backend evidence doc lists: each item's diff summary, per-test check
counts, API deltas, the I4 route chosen, any xnu citation produced for
TOO_LARGE (else "conservative" stands), and remaining unknowns.
Candidate stays isolated **PENDING** until reviewer acceptance of this
milestone — only then does Devin commit it.
