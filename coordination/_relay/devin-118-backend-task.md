# D118 backend — atomically exclude fd mutation and report quiescence correctly

Read `coordination/reviews/D116-20261019.md`. D116's normal tests pass,
but Codex reproduced fd aliasing with a deterministic temporary-copy
barrier: pause `d113_drain_close` after its adm count check; admit/pause a
drainer; close and reuse the fd; resume the write. Result:
`close_rc=0 drain_rc=1 fileA=0B fileD=71B`. A second focused test shows
`d113_quiesced()==1` while `FINAL` is held.

Work only in `analysis/d113-trace-transport/` and task-local tests. No
production/frontend/device/package/build/deploy/trustcache/restart/Git
operations.

1. Add an atomic bounded fd-mutation claim/phase to the `adm` word.
   `d113_drain_open` and `d113_drain_close` must claim it before validating
   admission counts and hold it through all fd close/open/publish steps.
   Drainer admission must fail while mutation is claimed. Release the phase
   on every error/success path. Keep filesystem calls outside producer lock
   context and don't add waits.
2. Make `d113_quiesced()` false while FINAL, FREEING, or fd mutation is
   active, consistent with its documented contract.
3. Add barrier tests matching the exact failing interleaving for close and
   open/rebind, with a disposable fd-reuse target proving records never
   land in the replacement file. Add phase-predicate test while final drain
   is paused. Preserve D115/D116 tests and owner-serialized raw-pointer
   contract.
4. Run L1–L12, C0–C12, all D112 reference/negative/prototype controls,
   and `git diff --check`; capture exact commands and exits.

Prototype remains **PENDING** and offline-only. Save one dated evidence
report plus relay result, clean task-owned temp artifacts, then stop with a
separate final line `(handover to codex now)`.
