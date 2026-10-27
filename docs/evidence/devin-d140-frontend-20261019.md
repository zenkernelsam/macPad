# D140 — SkyLight service-message timing instrumentation: offline feasibility

Date 2026-10-19. Packet `coordination/_relay/devin-140-frontend-task.md`.
Offline design only — all boundaries verified against `analysis/
SkyLight_22F82` (UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`,
function-starts parsed from `LC_FUNCTION_STARTS`, 12512 entries; 16-byte
prologues extracted verbatim). No source edits, no build, no device.

## 1. Boundary map — hookable vs not

| Timing edge | VA | Function start | Offset in image | Entry hookable? |
|---|---|---|---|---|
| pass entry/exit | `CGXRunOneServicesPass` `0x18542163c` | =itself | `0x33C63C` | YES — `pacibsp` `7f2303d5 ef3bb66d ed33016d eb2b026d` |
| recv return | `0x185421978` | `0x18542163c` (+0x33c) | mid-function | **NO at site** — use existing `mach_msg_overwrite` interpose (§3) |
| dispatch begin | `_post_port_data` `0x185421e48` | =itself | `0x338E48` | YES — `pacibsp` `…fa67bba9 f85f01a9 f65702a9` |
| handler dispatch | `rendezvousHandler` `0x1851eb4a8` | =itself | `0x1064A8` | YES — `pacibsp` `…fd7bbfa9 fd030091 e00301aa` |
| impl invoke | demux `blraaz` `0x1851eb584` | `_CGXHandleMessage` `0x1851eb4e0` (+0xa4) | mid-function | **NO at site** — hook `_CGXHandleMessage` entry (`0x1064E0`, `pacibsp`) and read `_current_message` `0x1d8ba5b88` for the in-flight msgid |
| reply enqueue | `CGXPostReplyMessage` `0x1854213a4` | =itself | `0x33C3A4` | YES — `pacibsp` `…ff4301d1 f65702a9 f44f03a9` |
| flush boundary | `_push_out_message` `0x1854214fc` | =itself | `0x33C4FC` | YES — same prologue shape |
| actual reply send | `0x185421554` | `_push_out_message` (+0x58) | mid-function | **NO at site** — existing `mach_msg`/`mach_msg_overwrite` send-leg interpose (§3) |

Function-entry hooks are all `pacibsp`-led — the installed relocator
already handles PAC-prologue sites (the SENDEROBS gate `+0x318464` is
one), so trampoline feasibility is established by precedent.

**Unsafe-with-current-constraints sites**: the three mid-function edges
(recv-call `+0x338`/`push call +0x678` inside the pass; demux `blraaz`
inside `_CGXHandleMessage`; in-push `mach_msg`). The provider/relocator
contract is a function-prologue swap with a 4-word prologue pin — there
is no mid-function branch-instrumentation contract; attempting one
would hit fail-closed refusal anyway. All three have safe proxies above.

## 2. Existing permit registrations — none cover these offsets

Current `macws_register_hook_permit` callsites: SkyLight gate `+0x318464`
(:6134), IOMFB `publicSwapEnd` (:6425), QC `enableTagList` (:19992),
IOMFB begin/finish (:20127/:20129), plus the three SENDEROBS sites under
the dedicated sentinel. **Zero** registrations cover the services-loop
VAs — each new entry hook needs a new permit
(`uuid=96676A53…`, offset above, 4-word prologue constant above), which
the fail-closed provider requires before `MSHookFunction`.

## 3. Zero-new-hook edges — existing interposes carry them

`DYLD_INTERPOSE` already wraps `mach_msg` (:15437), `mach_msg_overwrite`
(:15438), `mach_msg2_internal` (:15439) in every libmachook-loaded
process including WS. The wrappers already capture tid/pid/msgh_id/
ports under the `MACWS_MACH_MSG_ALL` umbrella gate — the services-pass
`mach_msg_overwrite` (opt `0x3000903`, send+rcv atomic) is exactly the
render-server receive loop these were built to observe. A record-only
leg under the new sentinel adds: timestamp at call/return, `tid`, recv
`msgh_id`/ports, send target — bounded ring write, no I/O in-window.
That gives the true userland recv edge and the true send edge that no
function-entry hook can reach. `mach_msg2_internal` covers the
libdispatch-internal receive path if it ever becomes relevant.

## 4. What a record-only hook can capture (contract check)

- **Monotonic timestamp**: `CLOCK_MONOTONIC` — must match the D107/D109
  pace-record clock domain; pairing is invalid otherwise. Pin it.
- **services tid**: `pthread_threadid_np` at every record — tid is
  *measured*, satisfying D129's missing current-generation thread
  identity rather than assumed.
- **Request correlation**: recv edge reads `receive_buffer->msgh_id`
  directly (existing wrapper already prints it); enqueue/flush hooks read
  `_current_message` `0x1d8ba5b88` for the in-flight request. CGX msgids
  (sender-tagged `pid.N`) make cross-edge joins unambiguous.
- **Generation**: records carry WS pid + image UUID (permit-pinned);
  generation is the process identity — pair records within one WS pid.
- **No return-value change / no locks across waits**: entry hooks call
  orig via trampoline; ring slot via bounded CAS/short critical section,
  released before orig runs. Pass-entry hook wraps (enter→orig→exit),
  bracketing the whole pass's work. Satisfies the contract.
- **What cannot be captured**: kernel arrival (send→port enqueue) stays
  userland-invisible — the design's honesty bound, carried from D129;
  and intra-pass message-order effects must be recorded (count of
  messages handled per pass) else a probe's dispatch-begin can lag for
  unrelated queued work.

## 5. Reconciliation with the D107/D109 pace-side contract

The pace candidate writes structured lines on a sentinel fd;
the SkyLight half should use the same discipline: dedicated sentinel
(e.g. `/tmp/macws_svctime`) consulted **only at SkyLight-load init**
(isWS gate — same as SENDEROBS: hooks cannot be armed into a running
WS), `CLOCK_MONOTONIC`, fixed-size records, bounded ring, flush outside
the measured window, errno preserved, per-record pid+tid, global seq.
Pair key = `(tid, timestamp ordering)` inside one armed window;
pace `enter_ns`/`exit_ns` + service `recv/dispatch/enqueue/send` edges
interleave on the same services tid if the D102 placement holds —
the records themselves verify that premise.

## 6. Minimum complete paired experiment

- Two cold starts of the same libmachook build (arming is install-time):
  **Arm A no-wait baseline** (pacing disabled or never triggered) and
  **Arm B bounded-wait** (one fixed interval) — identical WS UUID,
  workload, endpoint, thermal state.
- ≥30 sends/arm of the read-only `0x74a7` probe; sends phased inside and
  outside `[enter_ns,exit_ns]` windows on the Arm-B side.
- Records needed per send: recv ts, dispatch-begin ts, handler ts +
  msgid, enqueue ts, send-edge ts; pace enter/exit brackets with tid.
- Scoring per D131/D129 rule: dequeue/dispatch edges clustered at/after
  `exit_ns` (ε≈2 ms) + median excess ≥ max(8 ms, 0.5×interval),
  replicated on the measured services-tid → adapt; otherwise preserve.

## 7. Feasibility verdict

Complete chain is **feasible**: 6 function-entry permit hooks (prologues
pinned above) + record-legs inside 3 already-installed interposes cover
every edge except the three mid-function sites, which all have verified
proxies. Nothing requires mid-function hooking, return mutation, or
locks-across-waits. The only net-new code is the service-half record
implementation — the pace half already exists as the D107/D109 candidate.

(handover to codex now)
