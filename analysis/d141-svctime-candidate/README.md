# D141/D143/D150b — isolated SkyLight service-timing record-layer candidate

**Status: PENDING — offline-only prototype. Not linked, shipped, or integrated
into production libmachook; no device or GUI interaction exists or is
permitted for this milestone.**

This directory models the D140 service-half record layer: fixed-size records
covering the six SkyLight service-path entry edges plus the Mach interpose
call/return legs, pushed through a bounded nonblocking MPSC ring with a
single leased consumer.

## Files

- `d141_svctime.h` — record ABI, ring/init contract, binding contract lines
- `d141_svctime.c` — implementation
- `d141_svctime_test.c` — deterministic adversarial host-side suite
- `run_tests.sh` — ASan+UBSan run, TSan run, arm64 object compile

## Record ABI (unchanged, 64 bytes)

`{magic='SVC1', seq, ts_ns(CLOCK_MONOTONIC), tid(pthread_threadid_np),
pid, kind, msgid, aux, gen=(pid<<32)^uuid_low64}` — one fixed copyable
record; no pointers; `seq` is an attempt identifier (see D1).

Kinds: `PASS_ENTER/EXIT` (CGXRunOneServicesPass), `DISPATCH_BEGIN`
(_post_port_data), `HANDLER_BEGIN` (rendezvousHandler), `IMPL_INVOKE`
(_CGXHandleMessage), `REPLY_ENQUEUE` (CGXPostReplyMessage), `FLUSH_BEGIN`
(_push_out_message), `MACH_CALL`, `MACH_RET`.

## Ring protocol

- MPSC producers: CAS-claim `head`; `head − tail ≥ CAP(256) → drop + count`.
  Refusals never consume a claim index (head unchanged).
- Claim winner owns `slot[claim % CAP]` exclusively until its release seq
  commit (A3).
- Single consumer (process-lifetime tid lease, D5) drains only the
  contiguous committed prefix; stops at the first claimed-but-uncommitted
  slot — reclamation can never outrun a live producer write.
- Init: `UNINIT → INITIALIZING → ARMED|DISARMED`. One initializer probes
  the sentinel once (init-time-only; post-hoc arming impossible).
  Concurrent `gate_init` callers re-attempt the `UNINIT→INITIALIZING` CAS
  on every UNINIT observation (covers a `set_generation` window) and spin
  bounded across `INITIALIZING`; `g_gate_spun` counts entries into the
  losing-CAS path.

## Contract lines (binding)

- **D1** — drain order == claim order. `record.seq` is an *attempt*
  identifier allocated pre-claim, distinct from the slot-internal seq
  (claim index + 1). seq gaps = attempts that never reached the ring
  (bounded drop or dead-mid-emit), never committed-record loss; seq may be
  non-monotonic across producers — pairers must buffer reorder.
- **C3** — under documented order (`set_generation` before `gate_init`),
  no record emits before generation publishes. Reverse order yields zeroed
  `pid`/`gen` on records — forbidden in production wiring (asserted
  observable by T13).
- **A3** — only the head-CAS winner may write a slot; `s->rec`/`s->seq`
  stores belong solely to the claim holder.
- **D5** — consumer lease is process-lifetime, never released: a dead
  consumer permanently disables draining (records accumulate/drop;
  bounded). No `d141_drain` from signal/async context even on the owner
  tid — same-tid reentry is unserialized. Symmetric producer bound: a
  producer dying/stalling post-claim head-of-line-blocks the ring at the
  CAP bound; subsequent emits drop counted; the ring never corrupts and
  `g_dropped` honestly records the stall.
- **D8** — the armed timing window is exclusive with the
  `MACWS_MACH_MSG_ALL` umbrella trace. Enforcement belongs to the
  integration layer; this prototype flags the obligation only.
- **D9** — emit→push sequence, verbatim: armed acquire check → errno save
  → `g_seq` fetch-add → `clock_gettime(CLOCK_MONOTONIC)` →
  `pthread_threadid_np` → two relaxed loads (pid, gen) → `g_head` acquire
  load → bounded claim loop (`g_tail` acquire load + weak CAS per
  iteration) → optional `D141_TEST_HOOKS` callback → fixed 64-byte copy
  → release seq commit → one `g_emitted` increment (`+ g_dropped` on the
  refused path) → errno restore. No lock, `write(2)`, allocation, or
  blocking call.
- **D10** — in-candidate: emit/push/drain never call `mach*` or any
  wrapped function. Integration constraint: the same property must hold
  inside the production hook path.

## Mach-leg contract (I5)

- **CALL leg**: `msgid` = send-header `msgh_id` when
  `option & MACH_SEND_MSG`, else 0. For `mach_msg2` the send id comes from
  the packed register ABI (`id = hi32(arg4)`, `bits = lo32(arg2)`).
  `aux = option` verbatim. Send fields MUST be captured pre-call —
  `mach_msg` shares one buffer, so post-call `msgh_id` is the *received*
  id.
- **RET leg**: `msgid` = recv-header id iff `kr == MACH_MSG_SUCCESS &&
  option & MACH_RCV_MSG` (read `message`/`receive_buffer`/`data`
  respectively), else 0. `aux = kr` verbatim. Option bits are NOT carried
  on RET — they stay verbatim on the paired CALL's `aux`, recovered via
  the same-tid pairing rule.
- **MACH_RCV_TOO_LARGE**: `msgid = 0` — conservative: only `msgh_size` is
  defined; no header validity is claimed. **No XNU citation produced for
  this task — the conservative interpretation stands.**
- **Pairing**: nearest unmatched same-tid CALL — required for ASYNC
  reentrancy (signal-handler mach calls); ordinary trap nesting is
  impossible.
- Reply correlation `id − 100` is decoder-side; records carry raw ids.
- Timing legs must NOT inherit the trace-only 40000–41000 filter.
- Integration note: all three real wrappers (`mach_msg`,
  `mach_msg_overwrite`, `mach_msg2_internal`) can host both legs —
  verified at the record API only; no integration edits in this task.

## Pass-bracket contract (I4 — contract route chosen)

`d141_wrap_pass(fn, arg)` emits `PASS_ENTER`/`PASS_EXIT` with `msgid=0`,
`aux=0`. The per-pass *handled count* is NOT carried on the record; the
decoder derives it by counting `DISPATCH_BEGIN`/`HANDLER_BEGIN`/
`IMPL_INVOKE` edges between the same-tid ENTER and EXIT. Rationale: the
handled count is only knowable post-call, so carrying it on the record
would require holding state across the orig call (violates the
nothing-held-across-call rule); derivation is lossless because all three
edge kinds are emitted on the same tid inside the bracket.

## API deltas vs D141 baseline

| Change | Route |
|---|---|
| `d141_drain` → `ssize_t` (D143) | lease refusal returns −1 |
| `d141_wrap_pass(exit_aux, fn, arg)` → `d141_wrap_pass(fn, arg)` | I4 contract route — `exit_aux` dropped (it claimed post-call data at a pre-call-fixed value) |
| `d141_uncommitted()`, `d141_drain_refused()` (D143) | observability |
| `d141_gate_spun()`, `d141_head()`, `d141_tail()` (D150b) | read-only observability for T15/T16 |
| `d141_hook_gen_init` (D150b, `D141_TEST_HOOKS` only) | deterministic gen-window parking |
| `d141_gate_init` retry semantics (D150b I1) | UNINIT observed → re-CAS; `INITIALIZING` → bounded spin; `-1` only on pathological unsettled |

## Tests (`run_tests.sh`)

Deterministic; barrier/hook-driven, zero sleeps. ASan+UBSan **437 checks,
0 failures**; TSan **437 checks, 0 race reports**; arm64 object compiles.

| Test | Checks | Coverage |
|---|---|---|
| T1  | 71 | edge order/correlation, gen tag, PASS_EXIT aux=0 |
| T2  | 4  | saturation, exact drop accounting |
| T3  | 4  | errno + orig return preservation |
| T4  | 6  | nested pass reentrancy |
| T5  | 4  | disarmed: zero output, orig still runs |
| T6  | 5  | post-hoc arming impossible; gen locked at init |
| T7  | 4  | T11 alias (retained coverage) |
| T9  | 7  | paused-claim: drain stops, no skip-past, order preserved |
| T10 | 4  | foreign-consumer lease refusal |
| T11 | 4  | MPSC unique-msgid conservation |
| T12 | 3  | wraparound with open slot |
| T13 | 4  | concurrent gen-vs-gate: gate_rc==1 deterministic (I1); gen-won → coherent tag, gen-lost → zeroed pid/gen (C3) |
| T14 | 9  | 8 concurrent gate_init all observe ARMED |
| T15 | 6  | I1: G parks in `hook_gen_init`; T's gate CAS-loses, `gate_spun≥1`, wins re-CAS post-rollback → ARMED |
| T16 | 271| I2: exact CAP-boundary + flood — 255 commit, P parks claim 255, drain prefix, 255 followers, 1+CAP refusals, head never advances on refusal, ordered final drain, emitted=511/dropped=257/attempts=768 |
| T17 | 18 | I3: parked-prefix overlap + 4×300 churn gated on drain_started; per-record aux/magic/gen/tid/unique; conservation |
| T18 | 13 | I5: distinct-id join, `MACH_RCV_TIMED_OUT` exclusion (decoder rejects non-success RET; CALL stays unmatched; delayed success RET pairs to it), async interleave, aux verbatim, per-leg errno |

## Remaining known bounds

- Dead/never-committed producer permanently head-of-line-blocks the ring
  at CAP — bounded, never corrupting (D5 symmetric bound).
- Dead consumer permanently disables draining (D5).
- `gate_init` spin bound (40M) is pathological-exit only; `-1` is
  observable, callers treat as disarmed.
- No production integration, no packaging, no device path — by scope.
