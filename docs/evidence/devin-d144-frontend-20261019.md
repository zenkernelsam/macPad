# D144 — independent concurrency audit of frozen D141 svctime prototype

Frozen inputs: `analysis/d141-svctime-candidate/{d141_svctime.h,.c,
d141_svctime_test.c}` and D141 report; D142 matrix. D143 files not read.

## P0 — drain "skip uncommitted + advance tail" reintroduces the classic
MPSC slot-reuse race

`d141_push` claims slot index `c` via head-CAS, then writes `s->rec`, then
release-commits `s->seq = c+1`. `d141_drain` examines `tail..tail+limit`
(limit ≤ CAP), **skips** slots whose `seq != tail+i+1` (counts `g_skipped`),
then stores `g_tail = tail + limit` — advancing **past** uncommitted claims.

Reachable interleaving (preempted producer):

1. Producer P claims `c`, CAS wins, preempted before `s->rec` write.
2. Drain runs, sees stale `s->seq` on slot `c%CAP` → skip, advances
   `g_tail` beyond `c`.
3. Producer Q claims `c' = c + CAP` — the bound `c' − g_tail < CAP` now
   holds — and writes/commits `s->rec`, `s->seq = c'+1`.
4. P wakes, writes `s->rec = *r` **overwriting Q's committed record**, then
   stores `s->seq = c+1`.
5. Next drain at `c'`: seq mismatch (`c+1 ≠ c'+1`) → Q's record silently
   destroyed — `g_emitted` counted it, `d141_dropped` didn't; conservation
   `emitted+dropped==N` still passes while a committed record vanished.

Worst subcase: P's 64-byte `s->rec` memcpy can land while a later drain is
mid-`out[n++] = s->rec` on Q's slot → **torn record delivered as valid**
(matching seq, corrupted payload — garbage `ts_ns`/`msgid` that mispairs
downstream, worst possible failure for a timing-forensics tool).

`d141_skipped` also misreports: the skip is counted *before* P ever
commits; P's late commit lands in an already-consumed slot — the record
exists but is permanently undrainable.

### Minimal proof obligation for safe bounded reclamation

Claim: no producer may write slot `c % CAP` while any outstanding claim
`c' ≡ c (mod CAP)` is live. Sufficient rule — **drain stops at the first
uncommitted slot** (bounded drain returns only the contiguous committed
prefix; `g_tail = tail + (committed prefix length)`).

Proof sketch: `tail` only passes `c` after `s->seq == c+1` was observed
under acquire. Then `c + CAP` can never be claimed while `c` is
outstanding, because the push bound `c + CAP − g_tail < CAP` requires
`g_tail > c`, and `g_tail > c` implies commit `c+1` was observed, which
release-orders after `s->rec` fully written. A wedged producer stalls the
ring at CAP records; subsequent emits drop-and-count — bounded, honest,
detectable (`dropped` rises, no silent loss). Alternative "skip but retire"
schemes require per-claim tokens to make stale producers detect their own
retirement — strictly more machinery than stop-at-hole.

## P1 — `d141_wrap_pass` cannot populate the required per-pass count

Header documents "caller may carry handled-count in aux on PASS_EXIT" but
`exit_aux` is fixed **before** `fn` runs — the count isn't knowable yet.
No API path emits it. Fix: post-call aux (`aux_fn(uintptr_t ret)`) or
caller-visible state read after `fn`. As shipped, the record exists but
the required measurement field is unfillable — silent measurement gap.

## P2 findings

- **Record order ≠ seq order**: `.seq` is allocated (`g_seq` fetch_add)
  before the head claim; a preempted producer inverts them → drained
  stream can show seq 6 then 5. Contract must state: drain order = claim
  order; `rec.seq` is a correlation id, gaps = attempted-but-dropped.
  T1 asserts seq-monotonic (holds single-threaded); decoder must buffer
  reorder, not flag gap-then-fill as phantom loss.
- **seq allocated on drop**: emit allocates `g_seq` even when push drops
  → seqs count *attempts*; `emitted+dropped==N` is conservation of
  attempts, not committed records. Decoder rule needed.
- **Generation two-word tear**: `g_pid` and `g_gen` are separate relaxed
  atomics; `set_generation`'s `g_init==0` check is check-then-store — a
  concurrent (contract-violating but API-allowed) `gate_init` latches
  mid-set → torn (new pid, gen=0). Safe under the sequential-init
  contract; minimal hardening: derive `.pid = g_gen >> 32`, delete `g_pid`
  — one atomic carries both.
- **Drain not consumer-guarded**: two concurrent drains snapshot the same
  tail, double-deliver, and can regress `g_tail` via last-store-wins.
  Single-consumer is contract-implied, not enforced — add an owner flag
  or assert.
- **Combined-trap legs**: `MACH_CALL`/`MACH_RET` pair by same-tid
  adjacency; caller must pass send-msgid on CALL and recv-msgid (or 0 on
  `kr != SUCCESS` — buffer content unspecified) on RET. T8 uses the same
  msgid on both legs — models the wrong pattern; recv ids differ on
  combined `mach_msg_overwrite` sends+rcv. Contract line needed.
- **mach leg unpaired risk**: nested synchronous mach inside a handler
  interleaves CALL/RET pairs — pairing must tolerate interposition (deepen
  same-tid stack or tag depth in aux).

## Test adequacy — real coverage vs gaps

T1 ordering/correlation, T2 saturation drops, T3 errno/return, T4 nesting,
T5 disarmed zero, T6 post-hoc arm, T7 MPSC conservation, T8 leg aux.
Reported: 354 checks, 0 failures, ASan+UBSan.

**Coverage hole that hides the P0**: T7 joins all workers **before**
draining — zero produce/drain overlap; the claim-then-preempt window is
unreachable. `seen[]` loop writes `seen[i]=1` then checks it — vacuous
no-op (intended unique-seq check never implemented).

## Deterministic test matrix (required rows)

| # | case | required outcome |
|---|---|---|
| R1 | claim-hook parks producer pre-commit; drain runs; second producer wraps CAP; resume | no corruption — with stop-at-hole: drain returns prefix only, `skipped` never rises |
| R2 | drain thread concurrent with producers, 60s churn | drained records all `seq==tail+i+1` valid; no torn payload (poison pattern per slot) |
| R3 | seq-gap after drops | decoder treats gap as dropped, no misjoin |
| R4 | reordered seqs in stream (alloc-before-claim inversion) | pairer buffers, no phantom-drop flag |
| R5 | `set_generation` racing `gate_init` | either enforced exclusion or single-atomic gen — no torn pid/gen record |
| R6 | two concurrent drains | refused/serialized — no double-deliver, no tail regression |
| R7 | combined trap: CALL send-id X, RET recv-id Y, kr=0 | joined pair; X,Y distinct |
| R8 | RET kr=TIMED_OUT, msgid=0 | tagged non-arrival, excluded from join |
| R9 | nested mach inside handler (CALL/RET inside CALL..RET) | stack-ordered, no mispair |
| R10 | per-pass count on PASS_EXIT | populated via post-call aux |
| R11 | arm flip mid-push (contract: impossible post-latch — verify latch) | decision immutable, test asserts `g_init` once-only |
| R12 | wedged producer (simulated crash pre-commit) | ring stalls at CAP, drops counted, drain returns prefix, recovery documented |
| R13 | T7-style churn with real overlap (no join-drain serialization) | conservation holds under contention |
| R14 | vacuous-assert audit: every `seen[]`-style loop must test record fields | no self-asserting loops |

## Verdict

D141 is **PENDING — one P0**. Slot-reuse race must be closed (stop-at-hole
drain is the minimal rule) before any host test result is meaningful for
the armed path; P1 API fix required for the per-pass count field; P2s are
contract hardening. The P0 does not falsify the D140 design — it is a
ring-policy defect in the prototype, closed by rule change only.
