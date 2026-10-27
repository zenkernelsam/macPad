# D094 — user-authorized bounded read: sender-observation install witnesses

One bounded grep + correlation witnesses, read-only. `sshpass -e` +
transient `SSHPASS` only (never argv/files/logs/Git); exit 0. No
writes, restarts, trustcache, KRW/TCC, injection, or source changes.

## Collected

| Query | Result |
|---|---|
| `grep -cE "SENDEROBS-INSTALL\|CGXSYNTH-INSTALL" $LOGDIR/WindowServer.err` (`/var/jb/var/mobile/WindowServer.err`) | **0** — no matching lines |
| WS pid/lstart | **10447 / Mon Oct 19 00:17:23 2026** — same generation as D070/D091 |
| `macws_final_composite.state` | `state=ready`, `producer=10447`, `sequence=12384` (advanced from 11041), `reason=validated-final-composite` |

## Interpretation

- **Zero install lines is a missing witness, not a negative claim.**
  Every `#### …-INSTALL` trace is `fprintf(stderr,…)` → filtered by
  `macws_filtered_fprintf` (`mac_hooks.m:331–341`), which returns 0
  whenever the umbrella diagnostic is disarmed — and this production
  generation runs with it off. Absence of the lines says nothing
  about whether the sender-observation/CGXSYNTH hooks were attempted
  or installed in this generation.
- **Restart-free verdict: NO.** The D068 sender-observation plan
  cannot run restart-free on this generation — for the same
  structural reason as the frame-pacing diagnostic (D083/D085): the
  emit channel is umbrella-gated and the umbrella is a
  launch-latched confound. Any observable witness needs either the
  umbrella (confounding) or a new non-umbrella emit path inside the
  deployed dylib — which requires rebuild + redeploy + **WS restart**
  under the session/rollback protocol. Nothing on the filesystem can
  arm reporting in the running process.
- **Correlation**: the read was taken against WS 10447, `ready`,
  seq 12384 — a healthy, still-advancing generation; the missing
  witness is not explained by a dead or replaced desktop.

## Hygiene

Password only via transient `SSHPASS`/`sshpass -e`; unset after use;
endpoint and incidental host details redacted from saved artifacts.
