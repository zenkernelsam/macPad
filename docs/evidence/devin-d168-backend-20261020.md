# D168 — measurement integration boundary audit (read-only)

Date: 2026-10-20. Task: `coordination/_relay/devin-168-backend-task.md`.
Read-only: no source edits, builds, device access, or product
integration. Inputs: accepted candidate at commit `6d5f2e2`
(`analysis/d141-svctime-candidate/`), D164 amended plan
(`docs/evidence/devin-d164-frontend-20261020.md`), D166 uniqueness audit
(`docs/evidence/devin-d166-frontend-20261020.md`), D142 cautions
(`docs/evidence/devin-d142-frontend-20261019.md`).

Goal of the audit: map the smallest isolated observer/decoder milestone
that realizes D164/D166 same-generation `CALL→handler→reply` timing
under the serialize-one-probe rule — separating what already exists
from what is missing, and naming every gate.

## 1. What exists now (committed, offline-verified)

| Piece | Where | Realizes |
|---|---|---|
| 64B fixed record `{magic,seq,ts_ns,tid,pid,kind,msgid,aux,gen}` | `d141_svctime.h` | all join fields except `local_port` (absent by schema — D166 §1) |
| Nonblocking MPSC ring, contiguous-commit drain, single-consumer lease | `d141_svctime.c` | bounded drops (`g_dropped`), `seq` attempt ids (gap = never-committed, not loss) |
| `UNINIT→INITIALIZING→ARMED|DISARMED` gate, retry, `hook_gen_init` | same | same-generation tagging + sentinel-gated arming |
| Emit/wrap/leg API (`d141_emit`, `wrap_entry`, `wrap_pass`, `mach_call/ret`) | same | six entry edges + CALL/RET legs, errno envelope |
| Decoder-side pairing rule model | test `pair_for` | nearest-unmatched same-tid CALL; non-success RET → −1 (D162) |
| Measurement design + correlation rule | D164 §§2–8, D166 §4 | arms, join keys, serialize-one-probe, seq-gap voiding |

Also in-tree but separate: the real 16-byte trampoline provider
(`b717fb9`), existing mach interposes that *could* host CALL/RET legs
(`mac_hooks.m`), D142-verified relocatable prologues at all six sites.

## 2. Missing work — smallest-first

### Boundary A — canonical record-stream format (offline, trivial)
A drain consumer produces `d141_record[]`; the decoder needs a
self-describing file: small header (magic, `D141_SVC_VERSION`, ring
`CAP`, `gen` tag, source pid, arm label) + record array + end marker.
Missing today — no serialization spec exists. Cheap, fully offline.

### Boundary B — decoder (offline, the core of the milestone)
A host-side tool consuming the stream and implementing exactly the
accepted rules — nothing more:

- **Generation filter**: restrict to one `gen` tag per window
  (cross-generation records rejected, per D165: no cross-arm record
  joins ever — cold starts mint new pids/gens).
- **Services-tid resolution**: identify the services tid by
  `PASS_ENTER` density (measured, never assumed).
- **Serialize-one-probe spans**: open span at `MACH_CALL(0x74a7)`, close
  at its paired `MACH_RET`; inside the span expect
  `HANDLER_BEGIN(0x74a7)` on the same tid; assert at most one open
  `0x74a7` span per window — a second identical-key CALL inside an open
  span → flag ambiguous, not pair.
- **Non-success RET exclusion** (D162 predicate): `aux != SUCCESS` ⇒ no
  pair, consumes nothing; delayed success RET still joins.
- **Seq-gap voiding**: ordinal pairing only inside `record.seq`-
  contiguous spans; a gap inside a span voids it (flagged — a gap can
  hide either leg, D166 §3).
- **Local-port absence**: schema cannot carry it — the decoder takes
  sender identity only as arm-level metadata (single-sender control per
  D166 §4); never infers ports from on-record data.
- **Arm statistics**: per-arm median `HANDLER_BEGIN − MACH_CALL`,
  exit-cluster count vs `max(8 ms, 0.5 × wait-interval)` (D138 rule),
  and honest per-window `dropped`/`uncommitted` reporting — nonzero
  drops in a probe window flag it, not exclude silently (D164 §8).

### Boundary C — synthetic stream corpus (offline, proof)
Hand-built streams exercising: clean probe; timeout-then-delayed-reply;
seq gap inside a span (must void) vs outside (must not); foreign-msgid
interleave; two-arm fixture; drop/uncommitted flagging. Same harness
style as the accepted suite — barrier-free, pure data.

## 3. Gates deferred to integration (NOT this milestone)

| Missing piece | Gate |
|---|---|
| Six SkyLight permits `(uuid, offset, prologue16)` | reviewer-accepted permit-table addition; provider fails closed without them |
| libmachook hook install calling wrap/leg API | separate integration milestone |
| Sentinel arming + `set_generation`-before-gate ordering | runbook/deploy change |
| In-process drain consumer + record transport off-process | production daemon/transport design (consumer lease is process-lifetime — D5: teardown must drain-to-file *before* exit or the tail is lost) |
| D8 `MACWS_MACH_MSG_ALL` exclusivity | integration-layer config |
| Serialized probe sender (`0x74a7`, one in flight) | arm-control tool/runbook — enforces serialize-one-probe at the *sender*, not the decoder |
| In-situ overhead calibration arm | post-integration measurement |
| Device cold starts ×2 arms, thermal control | operator scheduling |

## 4. Teardown analysis (required by the task)

- **Decoder milestone**: none needed beyond bounded file reads —
  end-of-stream marker + arm label in the header suffice.
- **Future integration** (flagged now): drain consumer is a
  process-lifetime lease — teardown order must be drain-to-quiescence →
  serialize stream → remove sentinel (post-hoc re-arm is impossible by
  design, so removal is safe) → process exit. Producer/consumer deaths
  leave bounded, honestly-counted state (D5 symmetric bounds).

## 5. Proposed smallest reviewable milestone

**"svcdecode" — offline decoder + stream format + synthetic corpus**,
under `analysis/` only, same acceptance model as D150b:

- Deliverables: stream-format spec, decoder source, synthetic fixtures
  + tests (per §2 boundaries A–C).
- Zero permits, zero hooks, zero device — fully provable offline.
- It is the *only* component of the D164/D166 pipeline that can be
  built and reviewed before any integration gate opens; every other
  item sits behind §3's table.
- Acceptance would also freeze the join semantics the later integration
  must produce — de-risking the integration review itself.

## 6. Honest state

No new field witness is claimed. Provider/F1/pixel state stands per
D164 §10 accepted evidence (provider real + fail-closed live; F1
reopen-freshness witnessed on Terminal via D139; D135 pixel capture for
its generation; composite-pipeline invariant hooks still permit-less by
design). No device, source, build, or product action taken in D168.
