# D202 — bounded offline drain→v3 serializer harness

**Status: PENDING — analysis-only. Not linked, shipped, or integrated
into production libmachook; no device or GUI interaction exists or is
permitted. Not a WS observer, measures no waiting, authorizes no
device/product integration.**

Finite host harness per D201 (corrected by D202): collect committed
records from the D141 ring into ONE bounded `D141_RING_CAP` buffer via
the leased single consumer, then serialize an exact d169 v3 stream file
(`sizeof(hdr)+n*64`, no end marker) and prove it with the accepted
`d169_stream_load`/`d169_decode`.

## API

- `d202_collect(&b, chunk, budget)` — appends into the batch; each
  drain call is bounded (`min(chunk, CAP−n)`), at most `budget` calls.
  **Never waits for a stalled claim** (one zero-return ends the loop);
  never drains-and-discards once full. Statuses:
  `COMPLETE` (head==tail) · `INCOMPLETE_CAP` (buffer full, more
  pending — caller takes another batch) · `INCOMPLETE_CLAIM` (stopped
  on a claimed-but-uncommitted slot) · `INCOMPLETE_BUDGET` (drain-call
  budget expired) · `ERR` (lease refusal / bad arg). Post-drain
  `emitted/dropped/uncommitted` snapshots are verbatim counters.
- `d202_write_stream[_file]` — exact v3 file (D203-repaired):
  **all validation before any filesystem mutation**; a pre-existing
  destination is never opened or truncated. Payload goes to a unique
  same-dir `mkstemp` temp → checked write/flush/close → atomic
  `rename()`; failures unlink only the owned temp. `uuid`/`uuid_valid`
  from `d141_get_uuid` (rc<0 → `uuid_valid=0`); `pid`/`gen`
  caller-supplied. Every record is magic/pid/gen consistency-checked
  pre-write (`W_INCONSISTENT`); errno is captured **at the actual
  failing op** (write/flush/close/rename — never a pre-fclose stale
  value). `flags` stays 0: no v3 header field claims completeness —
  `COMPLETE` is a quiescent-snapshot verdict only.
- `d202_collect` (D203-repaired): `b->n > CAP` rejected before pointer
  arithmetic; `budget` is **per-invocation** (`drain_calls` stays
  cumulative observability); a partial batch resumes by calling again.

## Tests (`run_tests.sh`, s1–s9)

| s | proves |
|---|---|
| s1 | emitted records → file → loader → decode scores 1 span; ws HANDLER merges strictly between CALL/RET (fillers spread real ts) |
| s2 | identity: uuid==accessor, gen==(pid<<32)^lo64; corrupt gen / wrong caller pid → `W_INCONSISTENT` + no artifact |
| s3 | >CAP emits → `dropped` verbatim in header and file |
| s4 | parked mid-claim producer → `INCOMPLETE_CLAIM`, `uncommitted` counted, ≤2 drain calls (no waiting); release+join → remainder |
| s5 | unopenable path → `W_IO`+`ENOENT`; `fmemopen` undersized buffer → `W_IO` mid-write, never false success |
| s6 | 64 records via chunk=8 → 8 bounded calls, contiguous seqs; capacity batching: full buffer + pending → `INCOMPLETE_CAP`, next batch captures remainder, no loss/dup |
| s7 | foreign-thread `d141_drain` → −1 + `drain_refused` (lease) |
| s8 | empty collect → `sizeof(hdr)`-only file loads, n=0 |
| s9 | no `set_generation` → accessor −1 → `uuid_valid=0` loads unverified |
| t1 | sentinel target preserved on `W_ARG` (NULL rec, oversize n) |
| t2 | sentinel target preserved on `W_INCONSISTENT` |
| t3 | funopen-injected write failure → `W_IO`+EIO, target intact, no temp left |
| t4 | stale `errno=ESPIPE` + close-failure injection → reported errno is the CLOSE's `EIO`, not stale |
| t5 | success replaces target atomically; loads as v3; no temp leftovers |
| t6 | `b->n > CAP` → `ERR`, zero drain calls |
| t7 | budget is per-invocation: `INCOMPLETE_BUDGET` resumes to `COMPLETE` (64 recs, 8 cumulative calls) |

`d141_reset_for_test` runs only after all producer/consumer threads are
joined — parked writers alone are not quiescent (D202 correction).

## Reviewer corrections (D204 acceptance)

- Corrupt `n>CAP` now sets `status=ERR` as well as returning `ERR`
  (t6 asserts both agree).
- `fdopen` failure removes only its successfully-created mkstemp temp;
  open failure no longer unlinks an unowned template pathname.

## Results

`bash run_tests.sh`: ASan+UBSan **176 checks, 0 failures** (16
scenarios); TSan **176/0** (s4/s7 threads joined pre-reset); arm64
`Mach-O 64-bit object arm64`. (Earlier D203 state: 175/0 — the added
status-agreement assertion accounts for the count.)

## Boundaries

No production consumer thread, no transport, no WS hook/permit, no
`_current_message` read, no `MACWS_MACH_MSG_ALL` interaction, no
timing conclusion. Links `d141_svctime.c`/`d169_svcdecode.c` read-only;
accepted candidates unchanged.
