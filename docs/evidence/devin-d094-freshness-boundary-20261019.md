# Closeout — pixel-boundary freshness map (gen-10447)

Offline audit only. Two pixel boundaries, one WS generation, distinct
freshness:

| Boundary | Latest artifact | Generation / seq | Mechanism |
|---|---|---|---|
| **iPad panel** (Host→Metal→CARenderServer→panel) | `docs/evidence/devin-d041-input-boundary-20261019/frame-*.png` (2732×2048, Oct-19 00:32; sha256 pinned: before `6d899749…`, after-cmdf `02363cb1…`, after-text `81bebbc6…`, reverted `1b7f2001…`, after-d/after-right `e07d0eca…` identical) | gen-10447, seq ~1176 | `macws_ipados_capture` → `CARenderServerRenderDisplay` on `CADisplay main` (`misc/macws_ipados_capture.m:1,30–48`) |
| **VNC framebuffer** (chroot OSXvnc RFB) | `docs/evidence/devin-d094-frame-20261019.png` (1366×1024, sha256 `1a9c309c…`) | gen-10447, seq 12526→12530 window | `misc/capture_vnc.py` over SSH-localhost forward |

## Freshness verdict

- **Verified: no panel-boundary capture exists after seq~1176.** Full
  tree scan post-00:32 yields only the D094 VNC PNG; the
  `d052/d055/d059-device-capture` files are log transcripts, not
  pixels.
- `state=ready` + seq 1176→12530+ validates **composite delivery to
  displayd**, not panel rasterization — a Host presentation
  regression after 00:32 would not surface in the state file.
- Corrected row: **panel path generation-witnessed at seq~1176
  (D041); delivery continuously validated; panel-boundary freshness
  thereafter unproven.** Closing it = one repeat
  `macws_ipados_capture` run (already-accepted mechanism), not a new
  gap class.
- Layer separation preserved: VNC framebuffer ≠ Host panel; the two
  boundaries must never be conflated in the matrix.

## Reopen generation + visible-window contract (preserved, unchanged)

- `macwshostd/main.m:1444` `WaitForWindowMetricsFlagsAfterGeneration`:
  PID-scoped `macws_window_metrics.%d.bin` sidecar (:1448),
  `header.generation > minimumGenerationExclusive` (:1475–1476),
  `entry.flags & requiredFlags` (:1490).
- `:1678` reopen callsite requires `MacWSStreamWindowVisible` **and**
  `previousGeneration` — a fresh generation produced *after* the
  exact reopen request (`:3866` comment: stale Visible bit or PID
  uptime is not a witness).
- `:1617` `RequestFinderBrowserWindow` — real Command-N menu action +
  `window-ready`/`no-visible-window` (:1643); monotonic generation
  bump :571–575.
