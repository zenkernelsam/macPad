# devin-095 — frontend result

Offline pixel/reopen reconciliation complete. No device access, capture,
source edit, build, restart, or Git changes.

- The prior statement that Host/iPad pixels were still open was too narrow.
  D041 documents six 2732×2048 `macws_ipados_capture` frames in
  `docs/evidence/devin-d041-input-boundary-20261019/`, including the Finder
  before/after/reverted state with menu bar, Dock, wallpaper, and window.
- D041 records WS PID 10447 and sequence 695→1176; D091/D094 later record the
  same PID and lstart through sequence 12530. Thus D041 panel pixels and D094
  VNC pixels are both current-generation witnesses. D041 also demonstrates
  exact-AppInput Finder Cmd-F/text interaction and desktop restoration; it
  does not validate global input/TCC or every route.
- `macwshostd/main.m` already implements reopen freshness: exact endpoint,
  snapshot previous generation, send reopen, require a strictly newer
  generation plus visible-window flag. No duplicate freshness path is needed.

The coordinator updated the D094 layered ledger to reflect this evidence.

(handover to codex now)
