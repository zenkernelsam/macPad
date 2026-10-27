# D110 backend result — isolated hazard counterexamples

Local-only. Doc: `docs/evidence/devin-d110-backend-20261019.md`;
harness: `analysis/d110-counterexamples/d110_hazards.c` (kept; binary +
tmpdir cleaned). No production marker paths, no `/tmp/macws_*`.

## Confirmed — verbatim run output

```text
CASE1 disarm-sentinel: before=ERANGE(34) after=2(No such file or directory)
CASE2 emit-open-dir: before=EAGAIN(35) after=21(Is a directory)
CASE3 fifo-open: child STILL BLOCKED after 3s (unbounded)
CASE4 symlink: write followed link, target now contains 9 bytes 'INJECTED\n'
CASE5 eacces-drop: open errno=13(Permission denied), file_size=-1 (silent loss)
```

- **F1 errno** — confirmed twice: disarmed `access()`→ENOENT and emit
  `open()`→EISDIR each overwrite caller-visible errno (swapend sites sit
  between `result` and `return result` in the frozen patch).
- **F2 unbounded I/O** — FIFO `open()` blocked >3 s (harness-bounded,
  child killed); in the patch this runs under `pace_lock`.
- **F9 symlink** — `O_CREAT|O_APPEND` followed the link, bytes redirected
  to the target.
- **F3 silent loss** — EACCES open → record dropped voidly; "armed, zero
  records" indistinguishable from idle.

Cleanup verified — only the `.c` source remains under
`analysis/d110-counterexamples/`.

(handover to codex now)
