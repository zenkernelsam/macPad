# D122 backend — preserve final-drain then close lifecycle after shutdown

D120 fixes post-CLOSED `drain_open`, but it also makes
`d113_drain_close()` return -1 after shutdown. That contradicts the header's
specified owner sequence `shutdown → quiesced → drain_final → drain_close →
free`; D120 documents the resulting open fd leak to process exit. Do not accept
a permanent descriptor leak as the repair.

Work only in `analysis/d113-trace-transport/` and task-local tests. Offline
prototype only; no production/frontend/device/package/build/deploy/trustcache/
restart/Git operations.

1. Keep `d113_drain_open()` permanently refused after CLOSED.
2. Restore an explicit safe `drain_final()` then `drain_close()` teardown path
   after CLOSED. Close must atomically exclude active drainers and FINAL/FREEING,
   run outside producer lock context, and be race-safe with `d113_free` under the
   documented owner serialization rule. Do not reopen the fd or allow a new
   producer/drainer after shutdown. Require final-drain completion before
   close if needed to preserve the documented order.
3. Reconcile the meaning of `d113_quiesced()` with teardown. Do not claim
   quiescence is permanently stable if a later legal close transition exists;
   document exactly when it is safe to call and make the predicate false while
   any close/final/free phase is active. Ensure free refuses if an fd remains
   open, or otherwise prove it is closed before freeing.
4. Add lifecycle tests for: post-CLOSED open refusal/no mutation; final drain
   preserves buffered output; close after final succeeds exactly once and sets
   fd=-1; close races with the documented phase/admission controls; free after
   close succeeds; free with an open fd refuses without leaking. Preserve
   D115/D116/D118/D120 regressions where compatible.
5. Run lifecycle/concurrency and D112 reference/negative/prototype controls,
   `git diff --check`, and report exact commands/exits. Save one evidence report
   and relay result, remove task-owned binaries, keep candidate PENDING, then
   stop with `(handover to codex now)` on a separate line.
