# D110 — isolated counterexamples for D109 F1/F2/F3/F9

Local-only reproduction of the frozen patch's emit pattern
(`analysis/d107-pacing-candidate/d107-pacing-diagnostic.patch` lines 23–29,
50) in `analysis/d110-counterexamples/d110_hazards.c` — verbatim
`open(path,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);write;close` and
`access(sentinel,F_OK)` shapes. All paths under
`analysis/d110-counterexamples/tmpdir/` (no `/tmp/macws_*` markers, no
production paths); child bounded by 3 s `WNOHANG`+`SIGKILL`; artifacts and
binary removed after the run.

## Verbatim output (run 2026-10-19, macOS arm64, clang -Wall)

```text
CASE1 disarm-sentinel: before=ERANGE(34) after=2(No such file or directory)
CASE2 emit-open-dir: before=EAGAIN(35) after=21(Is a directory)
CASE3 fifo-open: child STILL BLOCKED after 3s (unbounded)
CASE4 symlink: write followed link, target now contains 9 bytes 'INJECTED\n'
CASE5 eacces-drop: open errno=13(Permission denied), file_size=-1 (silent loss)
```

## Confirmed mappings

- **F1 (errno contamination) — CONFIRMED ×2.** CASE1: the *disarmed* path
  alone — `access()` on the absent sentinel — replaces the caller's
  `ERANGE` with `ENOENT`. CASE2: a failing emit `open()` replaces `EAGAIN`
  with `EISDIR`. In the patch, both happen between `result` computation
  and `return result` at the swapend call sites (patch 316–326, 334–344)
  and before `return` at the begin site — caller-visible errno is the
  diagnostic's, not the real call's.
- **F2 (unbounded-blocking I/O) — CONFIRMED.** CASE3: `open()` on a FIFO
  at the trace path blocked >3 s (bounded by harness; child killed). In
  the patch this `open()` runs while holding `pace_lock` (patch 258–307),
  so one stalled open serializes every pace wait in the process.
- **F9 (symlink redirect) — CONFIRMED.** CASE4: `O_CREAT|O_APPEND` (no
  `O_NOFOLLOW`) followed a symlink and wrote 9 bytes into the link
  target — any writer to the trace directory can redirect the file.
- **F3 (silent loss) — CONFIRMED.** CASE5: mode-000 trace file →
  `open()` `EACCES` → record silently dropped (`(void)`), file stays
  empty — the trace reports "armed, no records" indistinguishable from
  idle.

## Harness notes

- macOS lacks `timeout(1)`; the harness bounds itself (3 s `WNOHANG` in
  CASE3 — the only case that can block; all others are non-blocking by
  construction). Total runtime <5 s; exit 0.
- CASE4 first run showed 0 bytes because a *relative* symlink content was
  resolved against the link's directory (dangling → `ENOENT` silent drop —
  itself an F3 instance); corrected to an absolute link target, rerun
  confirmed the redirect.
- Source kept: `analysis/d110-counterexamples/d110_hazards.c`. Binary,
  `tmpdir/`, all test files removed — `ls` after cleanup shows only the
  `.c` file.

(handover to codex now)
