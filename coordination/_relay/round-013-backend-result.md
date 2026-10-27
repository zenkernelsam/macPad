# Backend R013 result — layered display acceptance

One frozen head (`c97e219`), one owned generation, read-only acceptance
inspection, one cleanup. No source, package, publisher Identifier, pacing,
compiler, TCC policy, entitlement, assertion or ownership change; native
SpringBoard/tccd/backboardd were not restarted. Installed R012 package and
R010 baseline backups are unchanged.

## Actions and true exits

- Pre-start inventory 19:49:56 HKT: native SpringBoard 872 / tccd 883 /
  backboardd 905; package `com.kdt.macosbooter 0.3.4` installed; provider
  inode 250671913 SHA-256 `b2ae21da…1423e7`; libmachook arm64e
  `47f78880…d9432` CDHash `b579715e…b04df`, arm64 `6de9b688…cbd18`
  CDHash `0bbcbb6f…d08`; all diagnostic markers absent; thermal 34.00 °C
  nominal.
- `timeout 300 bash /var/jb/usr/macOS/bin/macos_gui.sh start coexist
  --no-vnc` → `START_EXIT=0`, relay exit 0/timeout 0. Identities:
  WindowServer 80310, displayd 80405 (19:50:49), Dock 80529, Terminal
  80562, OSXvnc 80546→80670, macOS tccd 80165, authd 80153,
  systemstatusd 79898. MacWSHost 81325 appeared only at 19:57:14.
- `timeout 240 bash /var/jb/usr/macOS/bin/macos_gui.sh stop` →
  `STOP_EXIT=0`; cleanup SSH exit 0; no owned process remains; native PIDs
  unchanged; markers absent; package/provider/libmachook hashes unchanged;
  thermal 34.29 °C nominal.

## Layer results

1. **Decode/asset: FAIL.** Terminal exposed a valid endpoint and V3 sidecar
   (`macws_window_metrics.80562.bin`, generation 2, one visible window 31,
   no configure ack), but no window stream reached displayd. Inside the
   generation, `MTLCompilerService` pid 80741 aborted at 19:51:37 in
   `MTLCompilerObject::readModuleFromBinaryRequest` (F11H, SIGABRT), the
   same signature as R012's 19:33:48 crash, and `iconservicesagent` pid
   80735 aborted at 19:52:17 in `MPSLibrary::MPSKey_Compile`.
2. **Provider: PASS (load/callability only).** WindowServer maps the
   provider (UUID `5700E649…`) plus `libmachook_arm64.dylib`;
   `VERDICT=INSTALLED-ORIGINAL-CALLABLE`.
3. **WindowServer composite producer: no transport witnessed.** Stderr
   contains the AGX probe `requested=0x78 result=0xe00002c2 fallback=0x70
   result=0 selected=legacy-0x70` and zero `FINAL-COMPOSITE` lines in any
   log. IOMFB completion counters are unavailable because the diagnostics
   marker was off. The `--no-vnc` launcher line "graphics not yet
   witnessed" is a process-only readiness branch
   (`macos_gui.sh:686-694`) and is not producer evidence.
4. **displayd receive: ready, zero accepted/rejected.** Receiver ready on
   port 21507, protocol 8; 10 replay requests with `lookup=0 send=0`;
   0 `final-composite-accepted`, 0 `final-composite-rejected`, 0
   `frame-first`, 0 workspace entries, 0 stream subscriptions in the R013
   segment (lines 19297–19354). A replay lookup/send succeeded without any
   matching WindowServer replay log; ownership of that registration is
   unresolved and is not used as acceptance.
5. **System pixels: none.** No accepted surface and no readback; no pixel
   claim is made from readiness or flags.

State file remains the startup record:
`state=missing producer=0 sequence=0 updated=1792324249.663221
reason=displayd-start` (19:50:49). The gap is upstream of displayd
acceptance, not a receiver/state-writer failure.

## R016 reopen witness

Not run, because the strict witness requires a valid `G0>0` **zero-entry**
sidecar and a new Host request/result. Terminal's sidecar is G0=2 with one
visible entry, and no Host reopen request existed (Host app came up at
19:57:14; hostd log has no new session). No G1 and no result claim.

## TCC and input (separate)

`TCCPREFLIGHT service=kTCCServicePostEvent result=1` (exit 0) with no
explicit reply dictionary captured. `SLSPostEventRecord -> rc=1002`
(exit 0). Neither is an acceptance.

## Crash delta

29 `ExcUserFault` reports in 19:50:23–19:51:36, all
`EXC_GUARD / GUARD_TYPE_USER / namespc 7 / reason 9`, identical
faulting-offset signature; named processes include displayd, Terminal,
OSXvnc, SystemUIServer, pkd, iconservicesagent and others, and the
long-lived named processes survived. No tccd/authd/systemstatusd/XPC
bootstrap crash. The 19:51:37 F11H abort and 19:52:17 MPS abort are the
R013 decode failures; the 19:33:48 F11H report is R012-generation and was
only surfaced late.

## Limits

Composite completion counts require the absent runtime-diagnostics marker;
the replay-registration ownership is unresolved; `--no-vnc` does not
validate a graphics witness; no pixel readback was possible; R016's
zero-entry precondition was absent.

## Evidence

- `docs/evidence/r013-display-layer-20261018.md`
- `coordination/artifacts/R013-display-layer-20261018.json`
- `analysis/r013-display/` (generation/cleanup logs, displayd segment
  counts, WindowServer stderr, preserved logs, 33 crash reports, probe
  outputs, SHA-256 manifest)
- device: `/var/mobile/macws_r013_evidence_20261018/{logs,crashes,probes}`

(handover to codex now)
