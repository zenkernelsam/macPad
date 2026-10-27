# D130 backend result — post-reboot D127 runtime acceptance

Date: 2026-10-19 · Device: iPad13,11 T8103 iPadOS 16.3 (20D47) · Evidence:
`docs/evidence/devin-d130-backend-20261019.md` · Candidate: **PENDING**
(layers 1, 2, 4, 6 evidenced; 3 unreachable this generation; 5 partial)

## Pre-start (post-reboot persistence — PASS)

Package `ii 0.3.4`, all D127 hashes unchanged (libmachook `42b75cf3`/
`e84d992a`, rootfs provider `c97acda3`), all four CDHashes still in
`jbctl trustcache info`, `/var/mnt/rootfs/var/jb/usr -> /var/jb/usr`
symlink persisted and `ensure_jb_usr_bind.sh` exits 0 standalone, all
sentinels absent, no WindowServer. Stale `/tmp/macos_gui_start.state`
(phase=trust, dead pid) and `MacWSStartup.log` boundary (1282 B) captured.

## The one start attempt — SUCCEEDED

`macos_gui.sh start coexist --no-vnc` (pid 21584) → `phase=ready`
(127 s; SSH client timeout did not kill the remote run). Live:
WindowServer 22527, OSXvnc 22786, Terminal 22802, Dock, ControlCenter,
SystemUIServer, Finder, pbs, launchservicesd, macwsinputd 22502,
macwsdisplayd 22644, macwsinteropd 22664, ~40 launchd jobs.
`macws_control_probe`: `windowserver=yes`, `windowing ready=yes abi=1
capabilities=0x7f` (vs the `windowserver=no` baseline).

## Layered verdicts

| Layer | Verdict | Key evidence |
|---|---|---|
| 1 package/decode | PASS | hashes/CDHashes/slices persist post-reboot |
| 2 provider+permits | PASS (partial) | 17 fail-closed `REFUSED no permit` lines; composite publisher runs only via the permit-gated QC cancel-completion hook → `sequence=622` proves install+permit worked |
| 3 SENDEROBS event | NOT REACHED | gate/obs hooks install only at SkyLight-load when sentinel pre-exists; post-hoc arming cannot install in this WS generation; needs sentinel-armed cold start |
| 4 final-composite | PASS | `state=ready producer=22527 sequence=141→622 reason=validated-final-composite`; workspace graph: canvas 2732x2048, wallpaper/menubar/StatusBar real surfaces, Terminal/Finder/CC windows |
| 5 pixels | PARTIAL | `--no-vnc` disables the RFB bridge by design (framebuffer never published); displayd validated 622 composites, snapdone=1 on wallpaper; no byte-level pixel capture obtainable this mode |
| 6 exact-window F1 | **PASS live** | ConfigureWindow→Terminal win 27: `configureAck req=640x480 applied=640x469 seq=1`, gen 6→7; negative control: same record + `GlobalSystemSurface` flag → **rejected**, no ack/gen change — first live end-to-end F1 accept/reject witness |

## Anomalies / deltas

- Replay channel to WS: every `replay-request` answered
  `source=unavailable published=NO` while the real composite channel
  validated frames — replay-source register never populated. Channel
  anomaly documented; main path proven.
- `ExcUserFault_macwsinputd` ×2 at 22:42 — `EXC_GUARD`/`XPC_EXIT_REASON_FAULT`,
  non-fatal, inputd still alive, input sockets kept working.
- `ExcUserFault_codesign` at postinst time (install still rc=0).
- Composite pipeline hooks (StartComposite/EndUpdate/…) remain
  permit-less by source design — unchanged from D035/R012, not a D127
  regression; they gate the invariant-restorer hooks, not publishing.

## Cleanup

Removed `/var/mobile/rfb_dev.py` + `/var/mobile/d130_frame.raw`. No
sentinels created; no probes/debuggers left; symlink preserved; GUI left
running in `phase=ready`.

(handover to codex now)
