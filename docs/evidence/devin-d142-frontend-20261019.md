# D142 — adversarial audit of the D140 timing contract + D107/D109 candidate

Frozen inputs only: D140 report (`docs/evidence/devin-d140-frontend-20261019.md`),
D107/D109 candidate patch + parser + tests, `misc/cydiasubstrate_macos.m`
relocator, `libmachook/mac_hooks.m` interposes. No edits, build, deploy, device.

`coordination/_relay/devin-141-backend-{task,pointer}.md` exist — NOT read per
the moving-files rule. Required follow-up: audit D141 prototype files once the
backend freezes them.

## 1. Entry offsets / prologues / permits — verified against the real relocator

Decoded all six pinned prologues word-by-word against `macws_insn_pc_relative`
and `macws_insn_terminal` (`misc/cydiasubstrate_macos.m`):

| site (imgoff) | insns | pcrel/terminal hits | verdict |
|---|---|---|---|
| 0x33C63C pass | pacibsp; movk/mov-class ×3 | none | relocatable |
| 0x338E48 _post_port_data | pacibsp; stp ×3 | none | relocatable |
| 0x1064A8 rendezvousHandler | pacibsp; stp; add; mov | none | relocatable |
| 0x1064E0 _CGXHandleMessage | pacibsp; stp×2; add | none | relocatable |
| 0x33C3A4 CGXPostReplyMessage | pacibsp; sub; stp×2 | none | relocatable |
| 0x33C4FC _push_out_message | pacibsp; sub; stp×2 | none | relocatable |

- Stolen window = exactly 16 bytes (`symbol[0..16)`), matching the 16-byte
  permit prologue pins — size discipline correct.
- **Hazard found**: `CGXPostReplyMessage` and `_push_out_message` have
  *identical* 16-byte prologues. Harmless for permits (keyed by
  uuid+offset), but a prologue-only verifier without offset binding would be
  ambiguous — the existing `macws_permit_for` binds offset, so OK. Still worth
  asserting offset uniqueness at registration to catch table-editing bugs.
- **Boundary assumption to pin**: verbatim steal breaks any *mid-window*
  inward branch target. Function entries after `pacibsp` are canonical —
  risk is low but not zero; the permit install should be followed by a
  one-shot smoke call through the thunk before arming (test matrix row).
- Six permits needed; none exist today. Registration must occur before
  arm, keyed `(uuid 96676A53…, offset, prologue16)` — D140's claim holds.

## 2. Mach interpose send/receive semantics — one real trap-level subtlety

`mach_msg_new` wrapper (`mac_hooks.m:13217+`) already snapshots the send
header (msgh_id/bits/ports) **pre-call** and traces the recv leg **post-call**
— the structural hooks for entry/exit timestamps exist.

**Adversarial catch — combined send+rcv trap**: the services pass uses
`mach_msg_overwrite` with send+rcv flags in ONE trap (`option 0x3000903`).
That single call carries *two* logical edges: the send (reply/batched
message lands on the queue at trap entry) and the recv (message returned at
trap exit). Rules the design must state explicitly:

- send-edge ts for a combined call = **entry** ts, recv-edge ts = **exit** ts.
  Recording send ts at return would invert causal order vs the actual enqueue.
- The reply sent at pass-N entry is for a request *dispatched in pass N-1*.
  Pairing must join by msgid, never by pass order.
- A send-only leg on a combined trap may also carry the push_out_message
  reply — the send-side record must distinguish reply-class traffic by
  `msgh_id` class + remote port, not assume "send during pass = reply".

## 3. msgid / tid correlation — mispair surfaces

- **Join rule**: reply `msgh_id = request + 100` (decimal; corroborated by
  our own synth reply at `mac_hooks.m:12906`). Request subsystem ids end at
  0x74A8 → replies land 0x74CC–0x750C — no self-collision, but the pairer
  must join on `(msgh_id−100, msgh_local_port)`, not msgid alone — other
  subsystems' request ids in the same process can occupy reply-looking values.
- **`_current_message` is a single global** (`0x1d8ba5b88`): reading it at
  `CGXPostReplyMessage` entry mispairs if the handler cleared it early or a
  nested dispatch is in flight. Safer primary key: the reply header arg's
  own `msgh_id` (arg0 of PostReply) → `−100` gives the request directly.
  `_current_message` is a crosscheck only; record both, flag disagreement.
- **Port-set recv**: pass recv targets a port *set*; the actual member port
  arrives in the received header's `msgh_local_port` — record it, or requests
  on different member ports coalesce.
- **Non-CGX traffic on the same tid**: nested synchronous mach traffic inside
  handlers pollutes the per-tid window. Every record must carry enough class
  (port-set membership, subsystem msgid range, send/recv) for the pairer to
  exclude it — "same tid" is necessary, not sufficient.
- **Timeout edges**: `MACH_RCV_TIMED_OUT`/`MACH_RCV_TOO_LARGE` (already
  distinguished at `:13117`) produce recv edges with NO message — must be
  tagged `result`, never counted as arrivals. A timed-out pass is normal
  idle behavior; misclassifying it would fabricate "empty service latency".

## 4. Nested / reentrant calls — what the record stream must survive

- `_post_port_data` iterates a deferred list → **N rendezvousHandler
  invocations inside one ppd call**. The per-message dispatch edge is the
  rendezvous entry (arg = message → msgid), not ppd entry. ppd entry =
  batch-dispatch-begin once per pass.
- **The measurement itself is nested**: the pace wait runs inside a timer
  callback inside `_CGXHandleMessage` inside a pass. So the armed record
  stream is expected to show `pass.enter → recv → dispatch(timer).enter →
  pace.enter/exit → dispatch.exit → reply.send`. The pairer must accept
  pace records strictly inside a dispatch window and flag any outside.
- **Unclosed enter**: an exception/early return past a wrapper strands an
  enter record — pairer drops unmatched enters (D109 unclosed-enter rule
  carries over unchanged).
- **Reentrancy guard on the record path itself**: emit code must never call
  anything that traps into the instrumented calls (no mach_msg inside the
  ring write; memcpy/CAS only). One assert-level check: a nested-record
  counter that must stay 0.

## 5. Timestamp placement & observer cost

- One clock domain across both halves, pinned at build: `mach_absolute_time`
  (+ timebase conversion fixed at init) or `CLOCK_MONOTONIC` — D107/D109
  chose CLOCK_MONOTONIC; SkyLight-side records MUST use the same or the
  join is invalid. Pin whichever the pace half already emits.
- Placement in trampolined hooks: wrapper prologue reads ts before orig
  (enter), epilogue reads after (exit) — two clock reads + one ring slot
  per invocation.
- **Amplification**: the interpose legs run on *every* mach_msg in the
  process while armed — record emission must be ~2 clock reads + CAS +
  bounded memcpy (tens of ns), never mutex (priority inversion interacts
  with the very wait being measured), never write(2).
- **Mutual exclusion required**: `MACWS_MACH_MSG_ALL`'s post-call
  `write(STDERR_FILENO,…)` path MUST be off during a timing arm — a write(2)
  inside the measured region is self-distorting. The arm gate should refuse
  or suppress the umbrella trace.
- **Symmetric overhead**: the record legs must run in BOTH arms (baseline
  included) — "armed" flips recording on; it cannot flip hooks on/off per
  arm or the comparison loses symmetry.

## 6. Blocking / clobbering / errno — one confirmed deficiency

- **D107/D109 patch contains zero `errno` handling** — the pace emit path's
  `write(2)` can set errno and the failure path leaves it clobbered into
  the caller. SENDEROBS holds the errno-envelope contract (`mac_hooks.m:5935`
  header comment); the pace emit must adopt the same save/restore on BOTH
  legs (pre-call record and post-call record — `poll`/`nanosleep` callers
  read errno legitimately, e.g. EINTR retry loops).
- Return values: wrappers return orig's result verbatim; record writes must
  not touch w0 or the caller-visible return path (structural requirement,
  test row).
- Buffer reuse: `mach_msg_overwrite` reuses the caller's buffer; the recv
  record must copy header fields out *before* any subsequent call can
  overwrite — ring slot gets a private copy, never a pointer.

## 7. Queue-vs-producer causality — what the design may NOT claim

- Positive result proves **only** "the paced wait delays dequeue on the
  services thread". It says nothing about producer render delay without the
  pace-side half (D107/D109 records) — the two are joined by (tid, window)
  nesting, not asserted.
- **Clustering confound**: dequeue bunching at wait-exit could also reflect
  ordinary periodic-pass behavior. Required control: probe sends during
  wait-free passes in the wait arm must show NON-clustered dequeue;
  otherwise the clustering signature is unproven.
- **Kernel arrival remains unobservable** — send-side entry ts bounds it
  from above (message queued no later than send-return), recv exit ts bounds
  dequeue; the gap decomposes into producer-wait vs queue-wait only if pace
  records bracket the same wall-clock window. Stated limit, not a defect.

## 8. Test matrix for the D141 prototype (rows all required)

| # | adversarial case | required behavior |
|---|---|---|
| T1 | flip one byte in a pinned prologue on disk | REFUSED, no patch |
| T2 | register with wrong offset, right prologue | permit-for miss → REFUSED |
| T3 | constructed cbz targeting inside stolen window | REFUSED (already enforced) |
| T4 | errno=EDEADLK preset → instrumented call → check | errno unchanged |
| T5 | orig returns error rc | returned verbatim |
| T6 | combined send+rcv trap | both edges, send ts < recv ts |
| T7 | recv timeout edge | tagged non-arrival |
| T8 | reply join (id−100, local_port); unknown reply id | orphan quarantined, never fabricated |
| T9 | ring wrap | seq gap → decoder drops partial pairs |
| T10 | arm flip mid-pass | partial window flagged/dropped |
| T11 | non-CGX mach traffic on services tid | filtered, counted separately |
| T12 | nested reply mid-dispatch | ordered, correctly nested |
| T13 | 3-message batch recv | 3 rendezvous records inside 1 ppd |
| T14 | forced unclosed enter | pairer tolerates, marks incomplete |
| T15 | tid distribution over 60s | report all tids seen; single-tid assertion is data, not assumption |
| T16 | MACWS_MACH_MSG_ALL + timing arm simultaneously | refused/suppressed |
| T17 | record-path calls mach_msg | nested-record counter = 0 (assert) |
| T18 | post-install smoke call through thunk | normal return before arm allowed |
| T19 | ptrace/strace-style check: no write(2)/mmap inside measured window | ring ops only |
| T20 | overhead bound | per-edge added latency measured in harness, budget stated |

## 9. Residual verdict

D140's feasibility claim stands **with four amendments**:
1. combined-trap send/recv edge semantics must be stated (§2);
2. reply join key is `(msgh_id−100, local_port)` with `_current_message`
   demoted to crosscheck (§3);
3. per-message dispatch edge = rendezvousHandler entry, ppd entry is
   batch-begin (§4);
4. pace emit needs the SENDEROBS errno envelope (§6) — the only confirmed
   code-level deficiency found in the frozen candidate.

No boundary is unhookable under the current trampoline (all six prologues
decode clean). Mid-function call sites remain unhookable-by-design and all
have verified proxies.
