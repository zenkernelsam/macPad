# D116 backend — finish isolated drain/fd/teardown ownership contract

D115 fixes the reproduced producer check-then-increment race with one
atomic admission word and deterministic L1–L6 ASan cases. Frontend's
independent D115 audit and Codex review identify remaining source-level
gaps. Read `coordination/reviews/D115-20261019.md` and
`docs/evidence/devin-d115-frontend-20261019.md` first.

Work only in `analysis/d113-trace-transport/` and task-local tests. No
production source, frontend candidate, device, package/build/deploy,
trustcache, restart, or Git operations.

Close these exact boundaries, choosing and documenting a narrow design:

1. `d113_drain()` must enter admitted-operation ownership before it reads
   `t->fd`; no close/reopen may invalidate an fd used by an active drain.
2. Define an explicit single-owner/exclusive contract for `drain_open`,
   `drain_close`, `drain_final`, and `free`. If relying on the external
   owner, specify exactly which caller must serialize/join each operation;
   if adding an in-object phase/gate, keep it bounded and never wait under
   the pacing lock.
3. Make final drain exclusive and account for itself in lifecycle state.
   Prevent concurrent final drains, `free` vs final drain, double-free,
   reopen vs active drain, and close vs mid-write fd reuse.
4. Retain the unavoidable raw-pointer boundary: the owner must prevent new
   API calls and join calls that have entered but not attempted their first
   object access before freeing. No in-object word can protect a call that
   starts after the allocation is freed.

Add deterministic barrier-controlled tests for active drain vs close,
active drain vs reopen, simultaneous final drains, final drain vs free,
two free attempts, and rejected post-shutdown drain admission. Use a
disposable fd-reuse target to prove records never land in the replacement
fd. Run all D115 lifecycle tests, C0–C12, D112 reference/negative/prototype
controls, and diff-check with exact exits. Preserve framing/timebase/errno
and bounded-work semantics. Keep production integration **PENDING**.

Update the isolated contract and evidence, clean only task-owned temporary
artifacts, and stop with a separate final line `(handover to codex now)`.
