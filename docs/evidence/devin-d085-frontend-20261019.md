# D085 — frontend consistency review of D084 backend authorization text (rev 2)

Offline review only. No execution, device, credentials, build, deploy,
restart, source, or Git. **No execution approved.**

Subject: `docs/evidence/devin-d084-backend-20261019.md` revision-2
authorization text, checked against the frozen runner v3 bytes
(D080, sha256 `8bb90399…`), my D083 finding list, and my D084
diagnostic scope (sentinel `macws_frame_pace_diagnostics`, trace
`/tmp/macws_frame_pace.trace`).

## Verified — every D083 finding is addressed

All 23 findings (S1–S7, Z1–Z5, W1–W6, A1–A4) map to concrete text
changes; the revision table (rev-2 §"delta") is accurate. Notably
correct: one-attempt-total including STOP, root-over-ssh principal
named, 120 s bound + kill plan, complete userspace whitelist,
stderr folded into the sanitization contract, admissible-absence
requires completed walk, delta=0 flagged not passed, no-injection /
no-capture / no-sentinel exclusions now explicit (and my D084
sentinel named), interpretation bound tightened ("present ⇒ dispatch
arm possible only").

## Concrete residuals (all small; none blocking the packet's logic)

- **R1 — unevidenced persistence assertion (wording).**
  "`jbclient_*` calls are process-scoped to the python process and
  leave no kernel bookkeeping after process exit" is stated as fact.
  The post-`ps` python3-exit witness verifies only the *process* side;
  nothing verifies kernel-side bookkeeping. The likely truth is
  actually stronger for scope — the physrw primitive is jailbreakd's
  pre-existing facility, so this run *creates* no kernel state rather
  than cleaning any up — but as written the claim is unevidenced.
  Reword to "arming activates a pre-existing jailbreakd primitive;
  this run creates no new kernel state" or mark it declared-assumption.
- **R2 — whitelist command precision.** "sha256 of the two pinned
  device binaries" — `sha256` is not a procursus binary name; spell
  the actual tool (`shasum -a 256` or `sha256sum`, whichever the
  device provides). Also: `cat` of `macws_final_composite.state` lacks
  the namespace-qualified path — spell `/tmp/macws_final_composite.state`
  (iOS side, where the iOS-native receiver writes it), since `/tmp`
  otherwise reads ambiguously against the chroot `/tmp` the desktop
  uses. And state *when* the sha256 capture runs (before the python
  step) so the recorded identity binds the mechanism that ran.
- **R3 — admissible-absence slot formula off-by-one vs script.**
  Text: "every slot `0..min(maxindex,0x40)-1` printed". Script:
  `range(min(midx + 1, 0x40))` ⇒ prints indices `0..min(midx,0x3f)`
  inclusive. The formulas differ when `midx < 0x40` (text undercounts
  by one) and coincide only when `midx ≥ 0x40`. Harmless direction —
  the rule demands no more than the script emits — but the doc's
  authority is exactness; write "indices 0 through
  `min(maxindex, 0x3f)` inclusive".
- **R4 — residue witness covers `…/CrashReporter/Panics` only.**
  A *userspace* python crash report lands under the CrashReporter
  parent tree, not `Panics/`; the current three-dir find would miss
  it (a panic is still caught via pid/lstart + Panics, so this is the
  userspace-crash-log case only). Widening that find entry to
  `/var/mobile/Library/Logs/CrashReporter` (same `-maxdepth 1`) covers
  both; alternatively declare it under the existing
  "other scratch paths" blind spot — it currently sits silent between
  a declared dir and a declared blind spot.

## Checked — no residuals

- **Script-vs-text emit contract**: every claimed emitted field exists
  in the frozen bytes; `exit 9` matches `sys.exit(9)`; STOP names
  match 1:1; no claim in the text requires unimplemented script
  behavior. The 120 s bound and sha256 capture are correctly framed as
  *operator/whitelist* actions, not script behavior.
- **No conflict with no-capture / no-sentinel exclusions**: the run
  touches nothing under the desktop's namespace; exclusions explicitly
  name `macws_frame_pace_diagnostics`; the snapshot and the future
  D084 pacing measurement remain orthogonal grants.
- **Credential/hash capture**: `read -s`/`sshpass -e` keeps SSHPASS
  out of argv/history/files; hash covers fenced bytes + trailing
  newline with re-verify before use; device-binary sha256s are
  post-hoc record fields, not credentials — no capture ambiguity
  beyond R2's naming.

## Verdict

Revision 2 correctly implements the D083 fixes. Four residuals (R1
wording, R2 command/path precision, R3 formula, R4 find-scope) are
text-only edits around the still-frozen script — none requires runner
changes and none grants or implies execution approval. Packet remains
"ready to present" once these are folded in; the user's named grant
is still the gate.
