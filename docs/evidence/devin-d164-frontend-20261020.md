# D164 — paired service-wait measurement plan (post-D150b `6d5f2e2`)

Design only. Inputs: accepted D150b contract (header verbatim),
D138 audit, D140 feasibility, D142/D145/D150–D163 chain. No
integration authorized — the accepted milestone is offline-only.

## 1. Question (unchanged from D138)

Does the manual frame wait delay WindowServer service-message
dequeue/dispatch on the services thread? Not producer-vs-queue
causality — only "the wait delayed dequeue" is provable, and only
against a wait-free control arm.

## 2. Arms and sample rule (minimum two conditions)

- **Arm A — wait-free baseline**: pacing wait disabled entirely.
- **Arm B — single bounded variable**: one fixed wait interval.
- ≥30 `0x74a7` read-only probes per arm; identical payload, identical
  WS generation (pid+`gen` tag), same thermal state; each arm a cold
  start (sentinel decision at process init — no mid-run rearm).
- Score per arm: median `HANDLER_BEGIN − MACH_CALL` latency on the
  services tid for probe msgid `0x74a7`; exit clustering checked
  against `max(8 ms, 0.5 × wait-interval)` (D138 threshold rule).
- Report every arm's `dropped`/`uncommitted` counts with its
  distribution — a saturated ring makes an arm unscoreable, not
  silently biased.

## 3. Join keys (exact)

- **Generation**: `record.gen` — all analysis restricted to one
  WS pid+UUID tag; cross-generation records filtered, not mixed.
- **Thread**: `record.tid` — services tid *measured*, not assumed:
  validate by PASS_ENTER density collapsing to one tid.
- **Request**: MACH_CALL `msgid` = send-header id (`0x74a7`). Reply
  correlation is decoder-side `id − 100` — a reply (`0x750b` for the
  probe) enqueued on pass N belongs to a request dequeued earlier;
  join by `(msgid−100, local_port)` where port context exists — never
  by pass sequence (D142).
- **Leg pair**: CALL→RET by nearest unmatched same-tid CALL —
  required for async reentrancy only (signal-handler mach calls);
  ordinary trap nesting is impossible.

## 4. Edge placement (per accepted contract)

- Six SkyLight entry hooks (D140 offsets): PASS_ENTER/EXIT bracket
  (`0x33C63C`), DISPATCH `_post_port_data` (`0x338E48`, batch start —
  NOT per-message), HANDLER `rendezvousHandler` (`0x1064A8`,
  per-message boundary), IMPL `_CGXHandleMessage` (`0x1064E0`),
  REPLY_ENQUEUE `CGXPostReplyMessage` (`0x33C3A4`), FLUSH
  `_push_out_message` (`0x33C4FC`). All via `d141_wrap_entry`/
  `d141_wrap_pass` — orig's return/errno untouched.
- Three existing interposes host MACH_CALL/MACH_RET legs — no new
  mid-function sites; the three unsafe mid-function points (pass
  recv call, demux `blraaz`, push-side `mach_msg`) stay unhooked.

## 5. Mach leg semantics in the plan

- CALL: `msgid` = send-header id iff `MACH_SEND_MSG`, else 0;
  `aux` = option verbatim; send fields snapshotted pre-call
  (shared `mach_msg` buffer — post-call `msgh_id` is the received id;
  `mach_msg2` fields unpacked from the register ABI).
- RET: `msgid` = recv-header id iff `kr==MACH_MSG_SUCCESS && RCV`,
  else 0; `aux` = kr verbatim; option recovered from paired CALL.
- Combined send+receive trap: CALL(send id)→RET(recv id) — pair by
  adjacency/order, never msgid equality.
- Non-success RET (`aux != 0`): excluded from pairing (D162 predicate)
  — `MACH_RCV_TIMED_OUT` (`0x10004003`) RET with msgid 0 must not
  consume its CALL; a delayed reply still joins it.
- `MACH_RCV_TOO_LARGE`: msgid 0 (conservative — no header validity
  claimed without xnu citation).
- Timing legs must not inherit the trace-only `40000–41000` filter.

## 6. Pass bracket interpretation

- PASS_ENTER→PASS_EXIT on the services tid brackets one dequeue
  batch; the per-pass handled count is derived by counting
  DISPATCH/HANDLER/IMPL edges inside the bracket (I4 contract route —
  PASS_EXIT aux is 0 by design).
- Service wait per probe = `HANDLER_BEGIN(ts) − MACH_CALL(ts)` on the
  same tid — covers dequeue + demux + dispatch entry, not impl
  duration (`IMPL_INVOKE` edge gives the sub-interval when needed).

## 7. CLOCK_MONOTONIC limits

- Single clock domain consistent with pace records — cross-leg deltas
  valid; not wall-clock, not cross-process comparable.
- Armed-path overhead (one `clock_gettime` + tid + record copy) is
  inside the measured window on instrumented edges — bounded and
  identical across arms, so it cancels in arm-vs-arm comparison but
  not in absolute latency claims.

## 8. Drops / reorder handling

- `record.seq` = attempt identifier — gaps mean never-reached-ring
  attempts, not committed loss; seq non-monotonic across producers →
  decoder buffers/reorders by drain (claim) order.
- Producer stall = CAP-bound head-of-line block; `g_dropped` is the
  honest record. Any arm with nonzero drops inside a probe window is
  flagged in the report — no silent exclusion.

## 9. What D150b can measure vs missing pieces

**The accepted candidate provides** (offline-verified): 64-byte
records, bounded MPSC ring, contiguous-commit drain, single-consumer
lease, sentinel gate, generation tag, errno envelope, nonblocking
emit, all leg semantics per §5.

**Not yet existing — every one is a separate gate:**

| Missing piece | Gate required |
|---|---|
| Six SkyLight permits (uuid/offset/prologue) — provider fails closed without them | reviewer-accepted permit table addition |
| Hook installation in `libmachook` calling the wrap/leg API | integration code change (new milestone) |
| Sentinel arming path + generation set before gate | deploy/runbook change |
| Production drain consumer + record transport off-device | new daemon/transport design |
| D8 umbrella `MACWS_MACH_MSG_ALL` exclusion — enforcement is integration-layer | integration config |
| In-situ overhead measurement on services tid | post-integration calibration arm |
| Decoder implementing §3/§5 join rules | offline tool (low gate) |
| Device cold-start windows, thermal control | operator scheduling |

## 10. Provider / F1 / pixel state (accepted evidence only)

- **Provider**: real 16-byte trampoline provider packaged (`b717fb9`);
  mapping in WS directly witnessed by REFUSED lines; positive permit
  installation remains sole-callsite inference; pacing permits
  unwitnessed in all stored generations; the live 16-byte stub read is
  the only retrospective witness path (privileged transport
  unverified).
- **F1/input**: `SLSPostEventRecord` still returns 1002 (no accepted
  input path); the reopen-freshness mechanism was never exercised —
  D134's gen 6→7 was the input-side counter, not a reopen; the
  469-size clamp proved the real setter + negative control (L6).
- **Pixel**: D135 produced a real composited desktop PNG via native
  screenshot — L5 was satisfied for that generation; no
  current-generation pixel witness is claimed here.

## 11. Hard boundaries

This plan executes nothing. Measurement requires a separately
reviewed integration milestone covering §9's table plus device
scheduling; the D150b commit's acceptance explicitly excludes it.

---

## D165 amendment (2026-10-20) — arm/generation comparability

§2's "identical WS generation (pid+`gen` tag)" is corrected: a cold
start mints a new WS pid ⇒ a new `gen` tag, so generation identity
**cannot** hold across arms by construction. The requirement that
survives is:

- **Identical across arms**: SkyLight image UUID, instrumented
  libmachook build (sentinel armed in BOTH arms), probe msgid/
  payload, sender path, device/package state, thermal/workload
  quiescence, producer identity. The ONLY intended difference is the
  wait flag.
- **Within an arm**: all scored records share one `gen` tag and one
  role-resolved services `tid` (identified by PASS_ENTER density —
  tid values differ across cold starts and are never joined by value).
- **Cross-arm comparison**: distribution-level only (median latency,
  cluster counts) — no record-level joins across generations.

This preserves the original scope and all other sections.

---

## D166 amendment (2026-10-20) — correlation uniqueness

§3's `(msgid − 100, local_port)` overclaims: the 64-byte record
carries `msgid` + `aux` only — `local_port` is absent from the
schema; it may be used decoder-side only if the future integration
layer logs it elsewhere, never assumed on-record. Therefore:

- **Serialize-one-probe rule**: at most one `0x74a7` in flight per
  measurement window; the open CALL→HANDLER_BEGIN→(REPLY_ENQUEUE)→RET
  span is then unambiguous by construction.
- **Queued/batched fallback**: ordinal pairing (nth CALL(0x74a7) ↔
  nth HANDLER_BEGIN(0x74a7), same tid) only inside `record.seq`-
  contiguous spans — a gap can be either kind, so any gap voids
  pairing for that span (flagged, not guessed).
- **Single-sender arm control** substitutes for the missing
  `local_port`: no concurrent senders may reuse probe msgid values
  within an arm.

All other sections unchanged.
