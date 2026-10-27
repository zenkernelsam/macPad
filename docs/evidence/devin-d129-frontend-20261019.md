# D129 — causal timing design: frame-completion wait vs WS service messages

Task: `coordination/_relay/devin-129-frontend-task.md`. Read-only design;
no source/build/device/Git changes. Anchors: `mac_hooks.m:27489-27563`,
callsites `:20055` (`begin_skylight_update`), `:27607` (`SwapEnd` hook),
`:27688` (selector-5 fallback); SkyLight 22F82 `96676A53` offsets from
D100/D101; D102 stacks (gen 50574); D107/D109 record schema; D113/D115
transport status.

## 1. Owner/thread relationship — FACT vs THEORY

- FACT (source): `macws_coexist_wait_for_completion_slot` serializes
  itself on a **libmachook-internal** `pace_lock` — no SkyLight code
  ever acquires it; it cannot block messages. The wait body is
  `poll(wake_fd)|usleep` until `CLOCK_MONOTONIC` target (paced against
  previous completion), optionally shortened by an interaction wake.
- FACT (gen-50574 stacks, D102): a paced display-update path ran on
  `ws_main_thread` *inside* `CGXRunOneServicesPass` (timer →
  `update_display_callback` → `UpdateDisplays` → `present_update`). When
  the wait executes there, the **single-threaded services loop itself**
  is the serialized resource — requests can only queue in the kernel;
  no lock is involved.
- FACT (source): three callsites. `:20055` paces unconditionally when
  swap-cancel is on; `:27607`/`:27688` pace only when
  `finish_depth == 0`. Which thread each callsite runs on in the
  **current** generation is MISSING (no gen-10447 stacks; 50574
  evidence does not transfer).
- THEORY (must be tested, not assumed): if a callsite runs on
  `display.primary`, service blocking would be transitive through the
  display path (IOMFB `server+0x18/+0x158` locks; observed turnstile
  direction was timer→main, i.e., main was the waiter). Do not assume
  thread coupling — the records below carry `tid` precisely to settle
  this per-generation.
- FACT (source): the interaction wake socket
  (`MACWS_INTERACTION_WAKE_SOCKET_PATH`, `mac_hooks.m:27428-27461`) is
  fed by `macws_vnc_signal_interaction_wake` (VNC activity, `:10520`)
  and only **shortens the pace interval** via `activity_pace_us`. It is
  not a service-message wake and cannot substitute for recv evidence.

## 2. Timing points and record schema (default-off, record-only)

SkyLight boundary (exact VAs, all function-entry hookable except the
instruction-level recv edge):

| ID | Point | Address | Meaning |
|---|---|---|---|
| S1 | `_post_port_data` dispatch edge | `blraaz` `0x185421f5c` (entry `0x185421bb4`) | T_dispatch — dequeue→handler |
| S2 | `rendezvousHandler` | `0x1851eb4a8` | T_dispatch2 — post-select handler entry |
| S3 | demux routine invoke | `blraaz` `0x1851eb584` | T_impl — per-msgid |
| S4 | `_CGXPostReplyMessage` | `0x1854213a4` | T_enqueue (NOT send) |
| S5 | `push_out_message→mach_msg` | `0x185421554` | T_send — real wire send |
| — | msg id | `_current_message` `0x1d8ba5b88` (stamped `0x1853beb0c/18`) | request identity |
| — | recv-return edge | `mach_msg_overwrite` `0x185421974`/`0x1854219a4` ret `0x185421978`/`0x1854219a8` | T_recv — **first** userland-observable; instruction-level hook, heavier |

libmachook (already designed, D107/D109): pace `enter/lock/start/exit`
records with `{seq,pid,tid,ts_ns,target_ns,slept_us,iters,wakes,
poll_errs,reason}` + `call` records at all three callsites (finish-
depth bypass marked `skipped=1`).

Fixed-size record (32 B, in-memory ring, no disk I/O inside the
measured region):
`{kind u8, seq u16, flags u8, pid u32, tid u64, ts_ns u64, msgid u32,
port u32, aux u64}` — `kind ∈ {pace-enter,pace-lock,pace-start,
pace-exit,pace-call, msg-recv,msg-dispatch,msg-impl,reply-enqueue,
reply-send}`; `msgid` from `_current_message`; `aux` = swap_id or
target_ns. Emission inherits the D107/D109 contract: CAS-armed,
ack-before-publish `write(2)` under `/tmp/macws_runtime_diagnostics`,
≤1 Hz cached check, 8192-line/arm budget, `pid=` on every record,
`(window,seq)` join keys. All stamps `CLOCK_MONOTONIC` — same domain as
the pace loop.

**Arrival vs wake (explicit)**: kernel enqueue is unobservable in
userland; T_recv is the first point. "Arrived during wait" is proven
only by *send_ts ∈ [enter,exit]* (client-controlled) + *recv edge
clustered at/after exit_ns* — an inference from clustering, labelled
as such.

## 3. Controls and decision rule

Same generation, SkyLight `96676A53`, deployed libmachook hashes,
thermal nominal, fixed endpoint/port, single read-only probe:
`_XGetWindowServerTimebase` msgid `0x74a7` (impl `0x18543e148`,
`size==0x18`, no side effects), fixed cadence with phase-shifted send
offsets so sends land both inside and outside pace intervals.

- Arm A (baseline): pacing disabled → collect `{send→recv→dispatch→
  impl→enqueue→send}` for ≥30 requests.
- Arm B (one bounded wait): force fixed pace interval (e.g. 16.6 ms,
  no activity-shortening) → same probe.
- No Mach-port polling change; no adaptive change in this task.

Decide wake adaptation iff ALL hold:
1. recv/dispatch edges for sends inside `[enter,exit]` cluster within
   ε of `exit_ns` (queued-during-wait signature);
2. median excess `dispatch_lag`/`reply_lag` over baseline P95 ≥
   max(8 ms, 0.5×pace interval) — a request can pay at most one
   interval; below that the adaptation buys nothing;
3. effect replicates on the pace-tid that equals the services tid;
   cross-tid correlation without causation is insufficient.

Negative result = no clustering at exit_ns OR excess below threshold
→ keep current behavior, close the question. Absent recv-edge
clustering is **not** proof nothing arrived during the wait — it is
"no wait-attributable service delay" only.

## 4. D113 transport boundary + missing artifact

- `analysis/d113-trace-transport/` stays an **offline harness only**:
  usable for post-processing record joins/replay. It is PENDING
  production (D114 reproduced check-then-increment UAF; D115 set the
  external-owner contract) — it must NOT ship in-process. In-WS
  capture uses the D107/D109 `write(2)` ring, not d113.
- MISSING runtime access: a kernel/libdispatch-internal arrival
  timestamp (mach IPC kdebug/voucher) — none exists in saved evidence
  and none is obtainable under current constraints. If the recv edge
  itself cannot be patched (instruction-level site), the smallest
  remaining observable is function-entry S1 (`_post_port_data`), with
  T_recv approximated by pass-boundary; state the approximation.

(handover to codex now)
