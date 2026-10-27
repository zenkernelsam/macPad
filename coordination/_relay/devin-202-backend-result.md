# devin-202-backend-result

Status: **PENDING reviewer acceptance** — finite drain→v3 harness
implemented under `analysis/d202-drainser-candidate/` only; accepted
candidates and production unchanged; nothing staged, no commit.

## Delivered

- `d202_collect`: one bounded CAP batch, remaining-capacity drains,
  call budget, zero-return ends loop — statuses `COMPLETE` /
  `INCOMPLETE_CAP` / `INCOMPLETE_CLAIM` / `INCOMPLETE_BUDGET` / `ERR`;
  verbatim post-drain snapshots. No waiting, no drain-and-discard.
- `d202_write_stream[_file]`: exact `sizeof(hdr)+n*64` v3;
  uuid/accessor (−1 → `uuid_valid=0`), caller pid, per-record
  magic/pid/gen check pre-write (`W_INCONSISTENT`), `W_IO` + errno +
  unlinked partials.

D202 corrections all honored: explicit incomplete/capacity states,
caller-takes-next-batch semantics, joined-before-reset quiescence,
fmemopen write-failure injection, s6 capacity batching + tiny-chunk
multi-drain.

Bring-up defects fixed: uuid_low64 = first 8 bytes (was bytes 8–15 →
all-writes-refused); span scoring evaluates at RET close (fillers give
real ts separation for the ws HANDLER); `hook_post_claim` must be
cleared before main-thread emits (self-park deadlock).

## Exact results

`bash run_tests.sh`: ASan+UBSan **134/0** (9 scenarios); TSan **134/0**
(s4/s7 threads joined pre-reset); arm64 `Mach-O 64-bit object arm64`.

Not a WS observer, measures no waiting, no transport/hook/permit/
device/product integration or wake conclusion. Evidence:
`docs/evidence/devin-d202-backend-20261020.md`.

(handover to codex now)
