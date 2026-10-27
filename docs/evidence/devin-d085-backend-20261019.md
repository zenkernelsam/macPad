# D085 — backend cross-review of D084 frontend revised frame-pacing plan

Offline review only. Plan NOT applied. Subject:
`docs/evidence/devin-d084-frontend-20261019.md` (the D082→D083 revision).
Verified against the current checkout: `mac_hooks.m`,
`macos_gui.sh`, `docs/runtime-switches.tsv`,
`macws_diagnostic_flags.sh`, `misc/audit_runtime_switches.py`.

## Verified sound

- **Emit channel (Finding 1)**: the `write(2)`-record-file choice is
  correct and matches `macws_keyboard_latency_trace` verbatim
  (current lines :9082–9097 — `vsnprintf` clamp, `open(O_APPEND|
  O_CREAT|O_CLOEXEC,0600)`, single `write`, `close`). OR-ing into
  `macws_filtered_fprintf` would indeed un-mute every `####` line in
  every chroot process — correctly rejected. Dropping `%p` from the
  retrievable file is right.
- **Registration requirement**: confirmed — `clear_diagnostic_state`
  (:3531) sweeps only paths from `diagnostic_flag_paths` (registry),
  both namespaces; `production_preflight` (:3895–3903) rejects any
  registered survivor. An unregistered sentinel would strand stale
  arm state across restarts. The `.trace` file correctly stays
  unregistered (keyboard-latency precedent).
- **Umbrella count**: 12 confirmed (:279–290).
- **Acceptance predicate + interpretation rule** (Findings 5/6):
  sound and within discipline.

## Finding A — BLOCKING: the Finding-3 "fix" deadlocks arming

The revision replaces the uncached `access(F_OK)` helper with the
cached `MACWS_DEFINE_STARTUP_FLAG` macro — but under the launcher's
own mechanics a registered sentinel **cannot exist at WS launch**:

- `enable_experimental_if_requested` (:5746–5757) `rm -f`s every
  diagnostic sentinel, then `touch`es them **only** under
  `--diagnostics` (which requires `--experimental`, :5688, and arms the
  umbrella = the confound being avoided).
- `production_preflight` (:5957–5959, plus :6041–6043) rejects any
  registered flag surviving to launch.

So for a registered flag: created before launch → swept or rejected;
created after launch → the **cached** flag already latched false at
first call. There is no arming window. The only escape under the
cached design is a launcher edit adding a create-after-preflight hook
for this flag — a **4th file** the plan doesn't count.

The revision misread the convention: the keyboard-latency sentinel is
**registered AND uncached** (`tsv:362` + `:9074`) *precisely because*
post-launch arming is the only window. Registration buys cleanup;
uncached `access()` buys the ability to arm after the launcher has
finished. The uncached helper isn't the violation needing an
exception — it's the established pattern for this sentinel class.

**Correction: revert to the uncached helper** (D082 shape), keep the
tsv registration for cleanup, keep per-call `access(2)` justified by
the throttle bounds. 3 files stands.

## Finding B — self-witness emit is incompatible with post-launch arming

The `COEXIST-PACE arm=1/arm=0` trace pair at :6479/:6483 fires at WS
**launch** — before any post-launch-created sentinel exists. With the
(correct) uncached helper, those calls see sentinel-absent → the
helper returns early → the arm witness is never written. Finding 2's
fix silently dies.

**Correction: move the arm witness to first-use.** One
`static _Atomic int armed_acked` emit on the first trace-helper call
after arming: `COEXIST-PACE sentinel-armed arm=<value of
g_macws_iomfb_coexist_swap_cancel>`. This preserves the three-way
disambiguation at whatever moment the operator creates the sentinel,
keeps output bounded (one line), and needs no launch-time sentinel.

## Finding C — stale `.trace` can satisfy the acceptance check

`O_APPEND`, not registered, never swept → a prior generation's
`arm=1` line survives and satisfies the Finding-5 check ("trace
contains arm=1") falsely. **Correction**: the protocol must `rm` the
trace as part of pre-arm cleanup (same step as sentinel creation),
and/or the acceptance check must verify the trace's first line
postdates the generation's lstart. Same class as the D041 lifecycle
runbook gap — record it explicitly.

## Finding D — minor: systematic line drift

Cited `:335-343`/`:344` (filter/define) — actual `:331-340`/`:341`;
convention `:345-351` — actual `:343-349`; trace helper `:9081-9096` —
actual `:9082-9097`. Uniform ~+3 drift suggests a slightly stale
checkout. Re-pin all numbers before application; patch discipline
requires line-exact claims.

## Finding E — residual notes (non-blocking)

- `audit_runtime_switches.py` tolerates the unregistered `.trace`
  literal (keyboard-latency precedent) — still, running the audit is
  the declared check after editing the tsv; state it.
- Rollback path is referenced ("session/rollback protocol") but no
  longer restated — keep the D082 §5 specifics (revert dylib +
  restart) in the activation grant text.
- `macos_gui.sh` edits become unnecessary *only* under the uncached
  design — confirm no launcher change sneaks back in.

## Verdict

**Do not apply as revised.** Finding A is a functional deadlock and
Finding B loses the arm witness under the only workable arming model.
The corrections are small and stay inside the D041 pattern: uncached
helper + tsv registration + first-arm-ack emit + pre-arm trace `rm` +
re-pinned line numbers. Findings C–E are protocol/doc edits.
