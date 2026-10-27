# D094 — bounded pixel witness: captured after verified prior authorization

Initial worker attempt stopped because its environment had no endpoint or
credential. The coordinator then verified the endpoint/port against the
prior user-authorized D091 task and the existing known host key before using
the same transient-password SSH path for one read-only frame capture. No
capture prerequisite was improvised and nothing on the device was mutated.

## What exists locally (verified)

- `misc/capture_vnc.py` — the accepted readback path: a minimal RFB
  3.8 client (security type **None**, true-colour BGRA32, Raw rects)
  that writes one PNG per run; passwordless by design.
- D094 post-backend pre-capture state: `state=ready`, producer=10447,
  sequence=12526; WS pid/lstart **10447 / Mon Oct 19 00:17:23 2026**.
- After capture: same WS pid/lstart; state remains `ready`, producer=10447,
  sequence=12530. The sequence advanced during the capture interval.
- `nonblack`/`mean` precedent exists (`misc/stray_perf_loop.py:5725–
  5743`, `cgdump.py`) — threshold `max(r,g,b) > 8` sampled.

## Capture and pixel measurements

- Captured one VNC RFB 3.8 raw framebuffer over a temporary SSH
  localhost forward using `misc/capture_vnc.py`.
- Artifact: `docs/evidence/devin-d094-frame-20261019.png`.
- Dimensions **1366×1024**; sampled `nonblack` (`max(R,G,B)>8`) **1,379,290 /
  1,398,784 = 98.6064%**; mean RGB channels **70.547 / 82.507 / 54.429**
  (overall channel mean **69.161**).
- Visual inspection confirms macOS menu bar, Finder, and the lake/forest
  wallpaper are visible. This closes the current-generation VNC pixel
  witness, correlated with the same WindowServer generation and advancing
  final-composite sequence.
- This proves pixels at the VNC framebuffer boundary. It is not an independent
  Host/iPad-display readback or interaction acceptance.

## Credential and transport hygiene

The endpoint/port were taken from D091 and R031; the captured SSH host key
matched the previously recorded `[192.168.68.63]:2222` identity. The password
was supplied only in transient `SSHPASS` and passed with `sshpass -e`; it was
not placed in argv, a file, output, or Git. The SSH forward was terminated
immediately after capture. No writes, builds, deployment, trustcache, restart,
TCC/KRW, injection, or source changes occurred.
