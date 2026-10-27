# D083 — backend cross-review of D082 frontend frame-pacing sentinel plan

Offline review only. Plan NOT applied; this memo identifies defects and
ambiguities to resolve before any future application.

Subject: `docs/evidence/devin-d082-frontend-20261019.md` — the ≈7-line
plan adding `macws_frame_pace_diagnostics_enabled()` (uncached
`access("/tmp/macws_frame_pace_diagnostics", F_OK)`) and OR-ing it into
the two COEXIST emit gates at `mac_hooks.m:20056` and `:27395`.

## Verified accurate in the plan

- All four cited locations are line-exact in the current checkout
  (:273 umbrella cached-once, :9074–9076 sibling helper, :20056 gate +
  `<=4 || %600` bound at :20060, :27395 gate + mode-transition bound
  at :27400).
- Umbrella does activate an alternate display producer — using it to
  observe pacing is a genuine confound; the dedicated-sentinel motive
  is sound.
- Chroot `/tmp` = host `/var/mnt/rootfs/tmp`, and the both-namespaces
  sweep on disarm matches the documented landmine.
- Restart-bound activation (rebuild → sign/trustcache → WS restart
  under session/rollback protocol → sentinel-lifecycle grant) is the
  correct discipline; the plan honestly marks itself unmeasurable in
  the current generation.

## Finding 1 — BLOCKING: the gated emits are inaudible under the dedicated sentinel

`mac_hooks.m:341` defines `#define fprintf macws_filtered_fprintf`, and
`:334` makes the filter return 0 on `stream == stderr` whenever
`macws_runtime_diagnostics_enabled()` is false. **Every** `fprintf` in
the file — including both COEXIST sites — passes through that filter.
The plan ORs the new sentinel into the *site gates* but not into the
*filter*: armed alone, `/tmp/macws_frame_pace_diagnostics` lets the
gates pass and the `fprintf` calls still emit zero bytes. The plan's
"same stderr channel already used by `#### CGXSYNTH`" is exactly the
trap — that channel was only ever observed *under* the umbrella, which
is the confound being avoided.

Two ways out, each with a cost the plan doesn't state:

- **(a) OR the new helper into `macws_filtered_fprintf` (:334)** —
  then every *ungated* `fprintf(stderr,"#### …")` in the file (install
  traces, the `#### COEXIST API SwapCancel` emit at :27586, etc.)
  un-mutes whenever the sentinel exists → scope expands far beyond
  "two bounded emits". The unconditional launch-time traces would also
  fire if the sentinel exists at WS start.
- **(b) Switch the two sites to the D041-proven `write(2)`/record-fd
  emit** (the keyboard-latency probe at :9078+ uses one `write(2)`
  per record precisely because stdio is unreliable under
  launchd/chroot). This keeps the narrow contract but is a real
  conversion, not an OR.

Either way the change is no longer "≈7 lines, two call sites + one
helper" — it needs a third edit site and an explicit un-muting-scope
decision.

## Finding 2 — scope/confound: unstated coexist precondition

Site 1's emit lives inside
`if (g_macws_iomfb_coexist_swap_cancel)` (:20050); that flag is set
only when the kern_SwapEnd BL callsite verifies at WS launch (:6477).
`macws_coexist_activity_pace_us`'s other callers (:27538, :27605,
:27686) are likewise coexist-armed paths (:27566 gate). So **"no
output" under an armed sentinel is three-way ambiguous**: sentinel not
seen / bound not reached / coexist translation not armed this
generation. The plan never names this precondition nor proposes an
arm-confirmation emit. Before reading silence as data, the operator
must witness the `#### COEXIST: verified kern_SwapEnd BL` install line
for the generation under measurement — add to the witness spec.

## Finding 3 — sentinel lifecycle vs the file's own convention

`mac_hooks.m:343–349`: "Diagnostic sentinels are a **process-start**
contract. `macos_gui.sh` creates or removes them before launch, and
**production_preflight rejects any survivor**. Never stat them from
the IOGPU submit/completion hot path" (a real 39-access(2)/6 s
regression is cited). The plan adopts the uncached `access(F_OK)`
pattern from the keyboard-latency helper (:9074) — that helper is
itself an exception to the stated convention, and the plan doesn't
reconcile: (a) if `production_preflight` whitelists known sentinel
names, a new name must be registered there or a WS launch with the
sentinel present may be rejected — a fourth touched file beyond the
claimed count; (b) uncached per-call `access(2)` on a per-update path
is acceptable only because both sites are already throttle-bounded —
worth one sentence so a future port to a hotter site doesn't inherit
the pattern blindly.

## Finding 4 — minor: umbrella sentinel count

Plan says the umbrella ORs "2 env vars + 11 sentinel files"; the
actual count at :279–290 is **12** `access()` sentinels. Cosmetic, but
the plan's authority is line-precision.

## Finding 5 — witness/acceptance gap post-restart

The plan correctly requires WS restart under session/rollback, but the
acceptance predicate before sentinel arming should be stated: new
generation `state=ready` **and** sequence advancing **and** the
coexist-verified install line present (Finding 2). Otherwise a
measurement taken on a degraded generation gets misattributed to
pacing — the "readiness is not pixels" checklist item applies to the
measurement target too.

## Finding 6 — measurement adequacy (non-blocking, state it)

The emits (`interval=` pace floor, `slept=` actual wait; mode/age
fields on transitions) do answer the share-lesson comparison —
`slept ≈ interval` means the wait was purely artificial vs a service
message having arrived early — *if* the doc states that interpretation
rule. One line in the plan's §3 would close it.

## Verdict

**Do not apply as written.** Finding 1 makes the plan produce zero
observable output under its own sentinel; fix requires the
filter-vs-write decision and an honest line/site count. Findings 2/5
are witness-contract additions; 3 needs a preflight reconciliation;
4/6 are doc-level. All are correctable within the existing patch
discipline — none requires touching production behavior.
