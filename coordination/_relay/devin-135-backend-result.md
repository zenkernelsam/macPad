# D135 backend result — native pixel witness, live D130 generation

Status: DONE (read-only, single capture). GUI generation untouched.

## Layer 5 — RESOLVED within constraint

- `dispcap_probe` (existing chroot tool) →
  `/var/mnt/rootfs/private/tmp/d135cap_1.png` → pulled to `/tmp/d135cap_1.png`
  on the Mac, device copy deleted.
- PNG: 2732×2048 (= workspace canvas), sha256
  `c981cce82c2e973c8cf223d9846caebf91be65164fb87f016cfb685f0663f87c`,
  343,142 unique colors.
- Visual inspection: real Ventura desktop — Apple menubar with
  `Terminal Shell Edit View Window Help`, status icons, Dock-owned
  DefaultDesktop.heic wallpaper filling the canvas. Matches the workspace
  graph layer identities. **Not a black frame — direct pixel evidence of a
  composited desktop.**
- Correlation: producer=22527 (live D130 WindowServer) throughout;
  composite sequence 904→954→966 around the single capture.
- Caveat stated: `CGDisplayCreateImage` = WS-authoritative composite pixels;
  the literal iPad framebuffer still has no installed CLI witness.

## Foreground precondition — MET

MacWSHost log: `foreground=YES backgrounded=NO suspended=NO`,
`native Metal present 2732x2048 source=IOSurface status=4`.

## F1 audit vs WaitForWindowMetricsFlagsAfterGeneration

D130 did **not** exercise reopen freshness: gate requires a
`ReopenApplication` record → `generation > previous` + `Visible` entry;
D130 only sent ConfigureWindow (ack + gen 6→7 republish) and a rejected
GlobalSystemSurface variant. Reopen freshness remains unwitnessed; not
claimed. Reported honestly in evidence doc.

## Boundaries honored

No restart/stop/reinstall; no VNC; no sentinels; no app launch/reopen; no
source/build/package writes. Device left as found (phase=ready, composite
advancing). Exact commands/exits in the evidence doc.

Evidence: `docs/evidence/devin-d135-backend-20261019.md`
Candidate: **PENDING** (reopen-freshness F1 + iPad framebuffer witness still open)

(handover to codex now)
