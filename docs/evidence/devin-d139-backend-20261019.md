# D139 — Terminal reopen-freshness: WITNESSED

**Task**: `coordination/_relay/devin-139-backend-task.md` — exercise the
existing `RequestApplicationReopen` path exactly once on the already-running
Terminal, then verify the production predicate
(`generation > previous` AND `Visible`). No kill/relaunch/restart, no VNC,
no sentinels, no product-file changes.

## Prechecks (all passed)

| Check | Result |
|---|---|
| Terminal process | pid **22802**, `/System/Applications/Utilities/Terminal.app/…`, `Ss`, 55:52 elapsed, **no child processes** → not executing an interactive task |
| AppInput endpoint | `/var/mnt/rootfs/private/tmp/macws_app_input.22802.sock` present (srw) |
| Metrics (pre) | `gen=7 cnt=1`, entry `w27 flags=0x4d` → Visible(1)+Shadow+Resizable+Focused |
| iOS/Host health | windowing `ready=yes abi=1 caps=0x7f`, hostd 1821 up 5h10m, Host scene `foreground=YES suspended=NO` |
| Producer | `producer=22527 sequence=1299` advancing — same D130 generation |

## The single reopen invocation

Command (existing supported tool, chroot-side — the tool's own binary lives
at `/usr/local/bin/macwsworkspacectl` inside the rootfs):

```
chroot /var/mnt/rootfs /bin/zsh -c "/usr/local/bin/macwsworkspacectl reopen-process 22802"
```

Output (exactly one record, one run):

```
application-reopen-sent pid=22802 route=AppInputBridge
```

This is the existing lifecycle path: the tool first tries the standard
`kAEReopenApplication` AppleEvent via the process's Carbon PSN
(`GetProcessForPID`), and on `procNotFound` — the documented case for
directly-exec'd chroot children absent from LaunchServices' registry —
delivers the same `MacWSInputKindReopenApplication` record through the real
AppInput endpoint (`macwsworkspacectl/main.m:1109–1180`). No custom
injection; one send.

## Predicate outcome — PASS

Bounded read-only poll of `macws_window_metrics.22802.bin`:

```
t=1792423710.63 gen=8 cnt=1 [(w27, flags=0x4d)]   # and stable for 10 s
```

`generation 7 → 8` immediately after the single reopen, with the entry
retaining `Visible` — **the production
`WaitForWindowMetricsFlagsAfterGeneration` gate (`generation > previous`
AND a `Visible` entry) is satisfied by direct evidence.** This is the
reopen-freshness witness D135 reported as unexercised: a fresh generation
produced *after* the exact reopen request, with a visible-window entry.

## Post-verification

- Terminal still alive: pid 22802 `Ss`, elapsed 57:21 — no kill/quit.
- `macws_final_composite.state`: `producer=22527 sequence=1326` — same WS
  producer, composite still advancing (1299→1326 across the check).
- Metrics at poll end: gen=8, w27 flags=0x4d — unchanged window identity.

## Crash delta (honest attribution)

My single `macwsworkspacectl` invocation produced two
`ExcUserFault_macwsworkspacectl` .ips at 23:28 (`EXC_GUARD`,
`XPC_EXIT_REASON_FAULT`, `is_simulated:1`) — the record was already sent
(`application-reopen-sent` printed) before the tool hit an XPC guard fault
on teardown; same class as the earlier iOS-side dyld-fault records. Stack
unaffected; the fault is in the probe tool, not in the acceptance path.
`routined` .ips at 23:13 pre-date this task (iOS noise).

## Cleanup / boundaries

- Nothing written to source/build/package/device; no sentinels, no VNC, no
  UI change. Desktop and Terminal left as found.
- Artifacts: local evidence doc + relay only; no screen data captured.

## Residuals

- Reopen freshness for Terminal is now witnessed; the equivalent for a
  **freshly launched** app or System Settings remains unexercised (D137
  blocker still stands for that target).

Candidate: **PENDING** (per standing rule; one more required layer closed).
