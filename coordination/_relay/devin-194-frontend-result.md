# D194 frontend result — 2026-10-20

Frozen D190 main/UUID audit (hashes in report).
Report: `docs/evidence/devin-d194-frontend-20261020.md`.

## Verdict: CHANGES_REQUESTED

- **F1 BLOCKER** `d190_main.c:44`: `dlsym(sl, "_SLSServerPort")`
  passes the nlist-prefixed name — Darwin `dlsym` wants the C name
  `"SLSServerPort"` (man dlsym: "pass 'foo'… unlike the older dyld
  APIs which required a leading underscore"). Searches for
  `__SLSServerPort` → NULL → exit 3 always. Symbol IS exported
  (T @0x1853c3fc8); tests never reach main's resolve path.
- **F2** `d190_check_skylight_uuid` proves exactly-one-image-carries-
  UUID but never binds the resolved `slp` to it — fix is
  `dladdr(slp)` → `d190_image_lc_uuid(dli_fbase)` on the owning
  image.
- **F3** `d190_image_lc_uuid` walks load commands with no `cmdsize`
  sanity — `cmdsize==0` infinite loop, oversized → OOB; bound by
  `sizeofcmds`.
- Cleanup lines explicitly not judged (backend's D192 repair).

Verified verbatim: wire shape, packed reply layout, CALL/RET record
semantics (actual received id, not constant), v3 stream header,
one-shot structure, fail-closed gate, G15 verbatim, committed-header
includes.

Read-only; no edits/build/invocation/device; no commit.

(handover to codex now)
