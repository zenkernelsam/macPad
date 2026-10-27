# D200 frontend result — 2026-10-20

Exact-binary ABI audit of D198's proposed `CGXRunOneServicesPass`
entry `0x33C63C` (SkyLight-22F82, UUID 96676A53-… verified).
Report: `docs/evidence/devin-d200-frontend-20261020.md`.

## Verdict: feasible — two caveats

- **Pin exact**: `t` symbol @`0x18542163c` = offset `0x33C63C`;
  span `…163c–1d7f` (`pacibsp`@`:1d80` is a separate helper; the
  `:1e40` retab is NOT this function's).
- **16B window relocatable**: `7f2303d5 ef3bb66d ed33016d eb2b026d`
  = pacibsp + 3×stp, position-independent, no control flow; PAC
  pairing survives trampoline execution.
- **Single reachable exit**: one `retab` `:1cb0` (x0=w21); all
  non-fatal paths merge via `:1bd4` → entry/exit wrapper covers
  every reachable exit; args (int x0, double d0) pass through.
- **Internal waits confirmed**: the pass CONTAINS the services-port
  wait — `os_nospin_lock` + `mach_msg_overwrite`/`mach_msg` +
  voucher adopt/revert + timer/event-pass poll + `waitpid`. The
  function is one whole pass (wait+receive+dispatch+flush) — the
  D140 bracket unit; mach legs nest inside it on the same tid.
- **Uncoverable exits**: `__exit`@`:1d78` + `__stack_chk_fail` —
  process-death only, correctly excluded.

## Inferred (runtime pin-down needed)

x0 flag / d0 deadline semantics (caller idiom `f(1,0.0)` poll),
return contract, which port the internal receive uses, `__exit`
trigger (w20 bit28 fatal flag). No permit/hook overlap per D198 —
mach_msg proxies already emit the nested legs.

Read-only; no edits/build/permits/hooks/device; no commit.

(handover to codex now)
