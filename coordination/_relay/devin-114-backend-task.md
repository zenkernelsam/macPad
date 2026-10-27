# D114 — transport lifecycle and data-integrity fixes

Review/fix D113 only in isolated analysis artifacts, no production/frontend:
- shutdown flag check alone does not prevent an already-entered producer
  publishing after shutdown drains. Define quiescence/in-flight ownership;
  free only after producers+drainer have stopped. gen field currently plain
  mutable at drain_open but read by record: remove data race or forbid open
  during producers with executable lifecycle checks. Repeated open must not
  leak fd. Never fix by waiting on producer while pace_lock held.
- raw mach_absolute_time ticks are not CLOCK_MONOTONIC nanoseconds, and
  pthread_self identity is not pthread_threadid_np. Declare/convert units and
  identity explicitly, requiring validated timebase for cross-trace joins.
- truncation currently forces newline and can look like a valid full record;
  in-band loss/truncation flag or length-checked frame must reject it.
  Partial write + failing/partial marker cannot guarantee safe framing:
  reader must detect/reject corruption even if every marker write fails.
- snprintf may affect errno and runtime internals; test actual record()
  errno/heap/lock assumptions, not only adapter. Payload must escape embedded
  newlines or use validated length framing; no false valid records.

Add bounded concurrent producer/drain/shutdown and repeated-open tests plus
marker-failure, truncation and embedded-newline corruption cases. Preserve
reference/bad controls. Do not claim wall-time wait-freedom from bounded CAS
alone or two-drainer support merely from queue CAS. Save exact commands and
failures; diagnostic prototype only, no device/source/build/deploy/Git except
local task-only test builds. Explicit handover required.
