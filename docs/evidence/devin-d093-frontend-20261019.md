# D093 — decision packet: desktop acceptance state (post-D091/D092)

Offline summary only. No device, source, build, deploy, restart,
pixels, or Git. Sources: D070 checkpoint, D091 backend health check,
D092 matrix, D088/D089/D090 pacing plan chain.

## Accepted — live-confirmed this boot epoch (gen-10447)

| Item | Basis |
|---|---|
| Compositor producing validated final composites | `state=ready`, seq **6055→11041**, `reason=validated-final-composite`, producer=10447 — live SSH witness (D091 backend, user-authorized) |
| Generation continuity | WS pid 10447 lstart `Oct 19 00:17:23`; tccd 883/10300 unchanged — no restart/panic since checkpoint |
| Image identity | rootfs live-confirmed Ventura **13.4.1/22F82**, kernel 22.3.0/T8103 — the analyzed SkyLight `96676A53` image is the running one |
| Provider/F1 path | D037 PAC/provider probe ALL PASS (do not redo) |
| displayd validation | four `ready` checks; state file continuously written |
| Exact-AppInput delivery | D041 accepted (does not cross the TCC gate) |
| Reopen freshness | generation gate + ≥1 visible window, source-verified |
| decode/asset | accepted (static wallpaper substitute) |

## Open witness gaps — same generation, no staleness excuse

1. **Current-generation pixel artifact** — MISSING. Seq advance is a
   displayd-declared counter, not a rendered pixel. Closing read:
   one bounded present-scene capture (accepted readback/VNC path) +
   `nonblack`/`mean` numeric checks + correlated state seq.
2. **SENDEROBS-INSTALL witness for gen-10447** — UNKNOWN. Zero local
   hits; only a 10-14 gate-only `CGXSYNTH-INSTALL` (prior generation,
   pre-disp/impl instrumentation). Closing read: `grep -nE
   "SENDEROBS-INSTALL|CGXSYNTH-INSTALL" /var/jb/var/mobile/WindowServer.err`
   — read-only, not yet performed. Its result decides whether the
   D068 causal observation can run restart-free.

## Pending policy question

Runner-v3 `mac_policy_list` snapshot (`macws-policy-snapshot-v3`,
sha256 `8bb90399…`) — revision-2 authorization text is review-complete
(D085: all 23 findings applied; four text-only residuals listed).
**Awaiting the user's named grant**; no offline substitute exists.

## Frame-pacing implementation — separate authorization required

The D088/D089/D090 plan is internally consistent and ready, but is a
**paper spec** (D091 verified: zero `macws_frame_pace` in tree).
Implementation requires eight distinct grants, none implied:

1. source edits (~45 lines / 3 `mac_hooks.m` regions + tsv row +
   regenerated `macws_diagnostic_flags.sh`);
2. `misc/audit_runtime_switches.py` check;
3. libmachook rebuild;
4. redeploy + trustcache re-admission;
5. WS restart under session/rollback/watchdog protocol;
6. post-launch sentinel lifecycle (`rm` trace → `touch` sentinel →
   retrieve → `rm` both, `/var/mnt/rootfs/tmp` spelling);
7. witness reads (trace, state, pid+lstart) with the full acceptance
   predicate;
8. rollback standby (prior hash-pinned dylib + restart protocol).

Not recommended absent the named grant; current generation remains
unmeasurable until all eight land.
