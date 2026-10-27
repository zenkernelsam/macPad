# D120 backend — reject fd mutation after CLOSED

D118 has a reproducible shutdown-invariant defect. In
`analysis/d113-trace-transport/d113_transport.c`, `fdmut_claim()` rejects
`D113_DR_MSK | D113_PHASE` but not `D113_CLOSED`. A focused standalone
harness (without `d113_probe.c`, whose RTLD_NEXT wrappers require explicit
initialization) produced:

```text
open=0
shutdown quiesced=1
postshutdown open=0 quiesced=1 fd=3
```

Thus `d113_drain_open()` succeeds after shutdown and changes the fd while the
public predicate continues claiming stable quiescence. The same admission gap
may affect `d113_drain_close()`.

Work only in `analysis/d113-trace-transport/` and task-local tests. Offline
prototype only: no production/frontend/device/package/build/deploy/trustcache/
restart/Git operations.

1. Make `fdmut_claim()` refuse when `D113_CLOSED` is set, in the same bounded
   atomic admission check. Preserve its existing refusal for active drainers
   and phases. Do not reopen or clear CLOSED.
2. Add standalone or lifecycle tests proving post-shutdown drain_open and
   drain_close both refuse, do not change fd/generation/files, and
   `d113_quiesced()` remains true. Include ordinary pre-shutdown mutator
   success and FINAL/FREEING exclusion controls. Ensure the harness either
   omits `d113_probe.c` or calls `d113_probe_resolve()` before any wrapped libc
   operation; do not repeat the null RTLD_NEXT function-pointer crash.
3. Run the focused lifecycle/concurrency suites and applicable D112 controls;
   report exact commands and exits and `git diff --check`.
4. Write one dated evidence report and relay result; clean task-owned binaries.
   Candidate remains PENDING. Stop with a separate final line
   `(handover to codex now)`.
