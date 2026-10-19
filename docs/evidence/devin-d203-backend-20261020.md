# D203 — D202 writer/collector boundary repair

Task: `coordination/_relay/devin-203-backend-task.md`. Fixes in
`analysis/d202-drainser-candidate/` only; accepted candidates and
production unchanged; not committed.

## Repairs

1. **Destructive output removed** — `d202_write_stream` no longer
   opens/truncates the destination. All arg + magic/pid/gen checks run
   before any filesystem mutation; payload goes to a unique same-dir
   `mkstemp` temp → checked write/flush/close → atomic `rename()`.
   Failures unlink only the owned temp; `path` is never touched.
   `ENAMETOOLONG` on oversize temp path.
2. **errno provenance** — captured at the actual failing op:
   `_file`'s fwrite/fflush errno, the path variant's `fclose` errno,
   and `rename`'s errno are each read at their own site; the cleanup
   `fclose` can no longer clobber the reported value.
3. **Collector boundary** — `b->n > D141_RING_CAP` → `ERR` before any
   pointer arithmetic, zero drain calls. `budget` is now explicitly
   **per-invocation**; `drain_calls` remains cumulative observability;
   a partial batch resumes on the next call (no silent no-op).
4. **Documentation** — `COMPLETE` is a quiescent-fixture snapshot, not
   a concurrent-producer shutdown guarantee; `flags` stays 0 — no v3
   header field claims completeness (status lives only in the batch).

## New deterministic tests (t1–t7)

Sentinel-target preservation on `W_ARG` (t1) and `W_INCONSISTENT`
(t2); funopen-injected write failure → `W_IO`+EIO with intact target
and no temp leftovers (t3); close-failure injection with stale
`errno=ESPIPE` proves the reported `EIO` is the close's (t4); success
replaces target atomically, loads as v3, no leftovers (t5); corrupt
`n>CAP` rejected with zero drains (t6); per-invocation budget resumes
a partial batch to completion (t7). Injection via `funopen` +
`D202_TEST_HOOKS` `d202_hook_file` — no `/dev/full` assumption.

## Exact results

`bash run_tests.sh`:

- ASan+UBSan: `PASS: 175 checks, 0 failures` (16 scenarios) — at
  D203 handover; final accepted run below
- TSan: `PASS: 175 checks, 0 failures` — at D203 handover
- arm64: `Mach-O 64-bit object arm64`

## Reviewer corrections + final counts (D204 acceptance)

`coordination/reviews/D202-D203-20261020.md` accepted with limits.
Reviewer applied two boundary fixes plus one assertion:

- corrupt `n>CAP` now sets `status=D202_ERR` as well as returning ERR
  (t6 gained a status-agreement check → 176 checks)
- `fdopen` failure removes only its successfully-created temp; open
  failure no longer unlinks an unowned template pathname

Final independent results: ASan+UBSan **176 checks, 0 failures**;
TSan **176/0**; arm64 object compiles. Accepted limits restated: the
serialization `flags` do not certify completeness; `COMPLETE` means
only a quiescent-fixture head/tail snapshot; UUID high64 is
producer-declared metadata, not independently record-verifiable; no
WS hook, sender invocation, device run, paired wait measurement, or
wake decision is authorized.
