# D109 backend result — frozen pacing diagnostic review

Independent review of `analysis/d107-pacing-candidate/` only (patch +
`pace_trace_parse.py`). No frontend successor inspected; no source applied;
no runtime. Findings: `docs/evidence/devin-d109-backend-20261019.md`.

## Concrete defects (patch/parser line refs)

- **F1 errno not preserved** — `access`/`open`/`write`/`close` clobber
  errno with no save/restore; swapend emits sit between `result` and
  `return result` (patch 316–326, 334–344) → caller sees diagnostic errno.
- **F2 unbounded-blocking I/O under `pace_lock`** — wake/exit emits run
  inside the pace mutex; `open()` on a FIFO/stalled fs blocks unboundedly,
  serializing all waits (8192-line cap bounds count, not time).
- **F3 silent-loss channel** — every write failure dropped voidly,
  including the trace-cap marker itself → "armed+quiet" indistinguishable
  from "armed+write-failing".
- **F4 missing `pid` on wake/call/mode/trace-cap** — shared file +
  per-process seq space → cross-process seq collision; parser merges
  foreign records (`dup-seq` only) and overwrites enter fields.
- **F5 wrong-thread calls mislabeled 'skipped-pre-lock'** — TLS seq=0
  joins fold wrong-thread calls into the skipped class regardless of the
  real `skipped` field.
- **F6 dropped-vs-capped is global** — any `cap_hit` window re-labels ALL
  seq gaps as capped, including gaps in uncapped windows (parser 207–212).
- **F7 disabled path nonzero** — per-call `clock_gettime`+atomics +
  ≤1 Hz `access()` + unconditional `pace_iters++`/reason stores — off ≠
  uninstrumented.
- **F8 observer effect unaccounted** — emit I/O under lock delays the next
  wait's entry; absolute deadline absorbs it into next `slept_us`; parser
  never reports exit→next-enter gaps.
- **F9 trace file hazards** — `/tmp` world-writable, no
  `O_NOFOLLOW`/`O_NONBLOCK`; first-writer 0600 ownership → EACCES/FIFO
  feeds F2/F3.
- **F10 service_tid is inference-only** over possibly mixed-pid records.
- **F11** dead `skipped-call` anomaly key; exit code ignores all anomalies
  except `malformed` — corrupted traces exit 0.
- **F12 `req_us` = `interval_us` request, not `effective_interval_us`** —
  effective pace unrecorded except via wake/target_ns arithmetic.

## Holds

Sibling (not nested) umbrella gating verified; enter/exit snapshot pairing
survives mid-wait disarm; one `sentinel-armed` per arm window; truncation
guards correct; orphan/reorder detection directionally right. No
return-value or pacing-arithmetic changes — diagnostic-only intent intact.

(handover to codex now)
