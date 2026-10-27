# D147 frontend result — 2026-10-20

Feasibility audit of D146's no-restart witness plan. Report:
`docs/evidence/devin-d147-frontend-20261020.md`.

## Verdicts

- **O1 WS identity** — feasible, low privilege (`pgrep`/`launchctl`;
  pin `lstart` against pid-reuse; requires D130 gen still running —
  unverifiable offline).
- **O2 state-file binding** — feasible, unprivileged; `producer` pid +
  stable `lstart` + advancing `sequence` over two samples = live firing
  witness.
- **O3 raw `.err`** — feasible, root read: real path confirmed
  `/var/jb/var/mobile/WindowServer.err` (outer-launchd plist); only
  provider raw-write lines can exist there (filtered-`fprintf` fact);
  file may be absent after failed starts — absence+live-pid is itself
  an anomaly to record.
- **O4 patch-stub reads** — feasible-in-principle, privileged. All four
  offsets + expected prologues + stub pattern (`51 00 00 58 / 20 02 1F
  D6 / .quad`) source-confirmed (IOMFB `0x11cc`, QC `0x29285c`/`0x291288`/
  `0x291220`); TASK_DYLD_INFO walk gives ASLR-safe bases and a free
  CydiaSubstrate-mapped witness. **Unverified transport link**:
  `task_for_pid`+`mach_vm_read_overwrite` on this WS — precedent exists
  (`macws_7dtd_memory_probe.c` vs another process) but never exercised
  against WS; no in-repo WS reader exists → a small new diagnostic
  binary is required.
- Byte reads distinguish installed/not only — "refused vs never
  attempted" remains indistinguishable.

## Smallest safe next step

One bounded read pass over existing root SSH: O1 → O3 → O2 — no new
tooling, closes provider-mapped / publisher-firing / generation-binding.
O4 (new probe, dedicated task) is the only path that settles
pacing-permit state.

(handover to codex now)
