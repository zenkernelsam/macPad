# devin-085 — frontend result

Final consistency review of D084 backend revision-2 authorization text
vs the frozen runner v3 bytes and D084 frontend diagnostic scope.
Offline only. **Not approved.**

- `docs/evidence/devin-d085-frontend-20261019.md`: full review.

## Result

All 23 D083 findings are correctly implemented in revision 2 — the
delta table is accurate, the emit/STOP/exit-code claims match the
frozen script exactly, and the exclusions now explicitly name
`macws_frame_pace_diagnostics` (no conflict with the D084 pacing
scope; the two grants stay orthogonal).

## Concrete residuals (text-only, none blocking packet logic)

- **R1**: "arming … leaves no kernel bookkeeping after process exit"
  is unevidenced — the post-`ps` witness covers only the process.
  Reword to "activates a pre-existing jailbreakd primitive; creates no
  new kernel state" (the stronger, accurate claim).
- **R2**: whitelist precision — `sha256` isn't a procursus binary name
  (spell `shasum -a 256`/`sha256sum`); `macws_final_composite.state`
  needs the qualified iOS-side `/tmp/` path; state when the sha256
  capture runs (pre-python step).
- **R3**: admissible-absence formula off-by-one — script prints
  `0..min(midx,0x3f)`; text's `0..min(maxindex,0x40)-1` undercounts
  when midx<0x40. Harmless direction, wrong spec.
- **R4**: residue `find` covers `…/CrashReporter/Panics` only — a
  userspace python crash log lands in the parent tree; widen that
  entry to the CrashReporter parent or declare it in the blind spot.

No claims the script doesn't implement; no credential/hash-capture
ambiguity beyond R2; no conflict with the no-capture/no-sentinel
exclusions. Runner bytes stay frozen; all fixes are auth-text edits.

(handover to codex now)
