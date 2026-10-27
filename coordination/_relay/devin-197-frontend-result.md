# D197 frontend result — 2026-10-20

Final D195 main/scanner re-review (hashes in report).
Report: `docs/evidence/devin-d197-frontend-20261020.md`.

## Verdict: ACCEPT

- **F1 fixed**: `dlsym(sl, "SLSServerPort")` (main:46) C-name;
  p8 proves both directions on a fixture — C-name resolves,
  underscored spelling returns NULL (the exact bug shape).
- **F2 fixed**: `d190_addr_image_match` (probe.c:87-91) dladdr→
  dli_fbase→LC_UUID→memcmp; called at :66 after resolve + image
  gate, **before** the first `slp()` at :71 — ordering requirement
  met (dladdr is resolution-only, never invocation).
- **F3 fixed**: `d190_image_lc_uuid_bounded` — region min(sizeofcmds,
  hdr_bytes); cmdsize≥8 and ≤limit−off; duplicate LC_UUID→−1;
  truncated uuid payload→−1; ncmds overrun→−1; p9 covers all
  vectors synthetically.
- **Cleanup preserved**: D196-accepted `d190_cleanup_class` +
  four-arm countered switch unchanged; verified by read, no reruns.
- Exactly-one loaded-image gate retained under the ownership check.

Minor: `self_uuid` name-matches the test binary — self-checking,
test-helper scope only.

Read-only; no edits/build/invocation/device; no commit.

(handover to codex now)
