# D139 backend result — Terminal reopen-freshness WITNESSED

Status: DONE. Exactly one reopen lifecycle record, real endpoint, predicate
verified. No kill/relaunch/restart, no VNC, no sentinels.

## What ran

`chroot /var/mnt/rootfs /usr/local/bin/macwsworkspacectl reopen-process 22802`
→ `application-reopen-sent pid=22802 route=AppInputBridge` — the existing
path (AE `kAEReopenApplication` via PSN → `procNotFound` → the standard
`MacWSInputKindReopenApplication` record on the real AppInput endpoint).

## Predicate — PASS (direct evidence)

- previous generation: **7**; post: **8** (persisted 10 s).
- entry `w27 flags=0x4d` retains `Visible` (0x1).
- `generation > previous` AND `Visible` — the production
  `WaitForWindowMetricsFlagsAfterGeneration` gate satisfied. A fresh
  generation was produced after the exact reopen — the D135 gap is closed
  for the ordinary AppKit-app path.

## Correlation

producer=22527 constant; composite seq 1299→1326 (advancing); Terminal pid
22802 still `Ss` 57m elapsed — alive, not relaunched.

## Prechecks captured

Terminal 22802 (no children → no interactive task), socket present,
windowing ready/0x7f, Host foreground=YES, iOS uptime healthy.

## Delta / integrity

Probe-caused crash records: two `ExcUserFault_macwsworkspacectl`
(EXC_GUARD/XPC fault) at 23:28 — send already completed before the fault;
not a stack issue. `routined` ips pre-date the task. No files changed;
UI left as found.

Evidence: `docs/evidence/devin-d139-backend-20261019.md`
Candidate: **PENDING**

(handover to codex now)
