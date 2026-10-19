# D202 — bounded drain→v3 serializer harness (implemented)

Task: `coordination/_relay/devin-202-backend-task.md`. Finite offline
harness under `analysis/d202-drainser-candidate/` only; the three
accepted candidate dirs and production unchanged; not committed.

## Implementation

`d202_collect` appends into one bounded `D141_RING_CAP` batch buffer —
each drain call gets `min(chunk, CAP−n)` only, never drains past the
buffer, at most `budget` calls, and **never waits**: one zero-return
ends the loop. Statuses: `COMPLETE` / `INCOMPLETE_CAP` (full + more
pending — caller takes another batch) / `INCOMPLETE_CLAIM` /
`INCOMPLETE_BUDGET` / `ERR`. Verbatim post-drain counter snapshots.

`d202_write_stream[_file]` — exact `sizeof(hdr)+n*64` v3 file; uuid
from `d141_get_uuid` (−1 → `uuid_valid=0`); per-record magic/pid/gen
consistency check *before* writing (`W_INCONSISTENT`); I/O failure →
`W_IO` + errno + partial artifact unlinked.

## D202 corrections honored

- one CAP buffer, remaining-capacity drains, no drain-and-discard
- INCOMPLETE/CAPACITY explicit; partial never labeled complete
- no stalled-claim wait; parked fixture released+joined before reset
- `get_uuid` −1 → `uuid_valid=0`; caller-supplied pid (no pid accessor)
- deterministic write-failure injection via `fmemopen` (no /dev/full)
- s6 = capacity-limited batching + tiny-chunk (8) multi-drain proof

## Bring-up defects fixed

1. `uuid_low64` is the **first** 8 bytes (LE), not bytes 8–15 — my
   consistency check rejected every record (`W_INCONSISTENT`
   everywhere) until corrected.
2. Span scoring evaluates at RET close: synthetic ws HANDLER must
   merge strictly between the real CALL/RET timestamps — s1 emits 32
   filler records for guaranteed ts separation, midpoint placement.
3. `hook_post_claim` fires on every claim — it must be cleared before
   the main-thread follow-up emit or the test parks itself (deadlock
   found and fixed).

## Exact results

`bash run_tests.sh`:

- ASan+UBSan: `PASS: 134 checks, 0 failures` (9 scenarios)
- TSan: `PASS: 134 checks, 0 failures` — s4 parked producer + s7
  foreign drainer, both joined before `reset_for_test`
- arm64: `Mach-O 64-bit object arm64`

No commit; PENDING Codex review.
