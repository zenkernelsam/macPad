# D092 — layered acceptance matrix after the D091 SSH health check

Offline evidence update only. Incorporates the backend's
user-authorized read-only SSH health check
(`docs/evidence/devin-d091-backend-20261019.md`). No new device
contact, pixels, source, or Git.

## Newly confirmed witness (FACT)

**Live sequence advance on the current generation.** Same WS
generation as the D070 checkpoint — WS pid 10447 lstart
`Oct 19 00:17:23`, tccd pids 883/10300 unchanged (no restart, no
panic) — `macws_final_composite.state`: `state=ready`,
`producer=10447`, `sequence 6055 → 11041`,
`reason=validated-final-composite`. Plus rootfs identity confirmed
live: Ventura **13.4.1/22F82** (the analyzed image; matches SkyLight
UUID `96676A53-…`), kernel `22.3.0 … T8103`. This is the
"advancing sequence" witness — the compositor continuously produces
*validated* final composites — not uptime, not pixels.

## Layered acceptance matrix

| Layer | Status | Witness | Gap / exact closing read |
|---|---|---|---|
| decode/asset | ACCEPTED | static wallpaper substitute decodes; HEIC-black fix verified | none |
| provider/F1 | ACCEPTED | D037 PAC/provider probe ALL PASS, trampoline verified live | none — do not redo |
| producer/final composite | **ACCEPTED, re-confirmed live** | `macws_publish_scanout_after_swap` path + state seq **6055→11041**, same producer 10447, `reason=validated-final-composite` | none |
| displayd | ACCEPTED | four `ready` validations; state file continuously written | none |
| Host / visible pixels | **PARTIAL — stale artifact** | last pixel PNGs are D037/D039-era (prior generation); D091 seq advance is displayd-declared, not a pixel | **current-gen pixel artifact** — one bounded present-scene capture via accepted readback/VNC path + `nonblack`/`mean` + correlated seq. Not closed. |
| AppInput (exact window) | ACCEPTED | D041 exact-PID/window delivery, does not cross the TCC gate | none — do not redo |
| Global input / TCC | BLOCKED (policy) | `SLSPostEventRecord`→1002 at `CGXSenderCanSynthesizeEvents`; provider question unresolved pending the frozen runner-v3 grant | runner-v3 snapshot packet awaits the user's named grant; no frontend read substitutes |
| Reopen freshness | ACCEPTED (source) | generation gate + ≥1 real visible window requirement, verified in `macwshostd/main.m` | none |
| SENDEROBS installation | **UNKNOWN** | zero `SENDEROBS-INSTALL` lines in all local artifacts; only 10-14 gate-only `CGXSYNTH-INSTALL` (prior generation, pre-disp/impl) | **exact read**: `grep -nE "SENDEROBS-INSTALL|CGXSYNTH-INSTALL" /var/jb/var/mobile/WindowServer.err` on device — the D091 check did not include it; still open |

## Deliberate distinctions

- The D091 seq witness upgrades *producer* confidence to live-current;
  it does **not** close the Host-pixel gap — a displayd-written
  sequence is not a rendered pixel.
- Generation continuity (same WS pid/lstart, same tccd pids) means the
  current pixel and SENDEROBS reads would land on the *same* measured
  generation — no staleness excuse remains for either gap.
- `rootfs=13.4.1/22F82` live-confirms the analyzed image — strengthens
  every UUID/offset-pinned artifact (SkyLight `96676A53`, runner-v3
  kernel constants) for *this* boot epoch.
