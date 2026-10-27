# devin-087 — frontend result

Final self-audit of the D086 corrected plan. Offline only — nothing
applied, no execution implied.

- `docs/evidence/devin-d087-frontend-20261019.md`: full audit.

## Verified clean

All line pins exact (sites :20056/:27395, bounds :20060/:27400, filter
:341, launcher :5725/:5689/:3768/:5959/:6043); uncached post-launch
arming genuinely works (no mid-session sweep); output scope stays at
the two sites + trace file, `%p` dropped; registration
(`flag`/`off` → sweep + preflight + audit) and rollback
(pinned-dylib revert + restart + dual-namespace rm) complete. Checked
`state`-kind registration for `.trace` — flag-list is flag-kind-only,
so protocol rm is the correct mechanism; unregistered is right.

## Concrete residuals

- **R1 (new, shared by D086 and backend's A-correction):** the
  uncached `access(F_OK)` sits in a *per-frame* gate inside
  `begin_skylight_update` — `access(2)` at frame rate is the exact
  39-calls/6 s regression class the :343 convention cites. Emit
  throttles never bounded the gate evaluation; the keyboard-latency
  precedent is per-keystroke, not per-frame. Fix (planned): rate-limit
  the check to ≤1 Hz via last-check timestamp — post-launch arming and
  live disarm preserved at ≤1 s latency, per-frame cost one atomic
  load.
- **R4 (merge into R1 fix):** the `sentinel-armed` ack inside the
  trace helper fires at first *bounded emit*, so "armed, bounds never
  hit" still yields an empty trace. Emit on the check's 0→1 armed
  edge instead — ack exists within ~1 s of arming regardless of
  emits; disarm→re-arm gets a fresh ack per window.
- **R2:** normal-path cleanup unstated — add retrieve → rm trace +
  rm sentinel (dual namespace) to the protocol.
- **R3:** process-latched ack would miss re-arms — superseded by the
  edge-triggered design.

## Verdict

D086 resolves all five backend findings as designed, but R1 leaves it
un-applyable under the file's own hot-path rule. Fix is ~10 added
lines (rate-limited check + edge-emit ack); honest size ≈45 lines
across 3 `mac_hooks.m` regions + tsv row + regenerated flags. No
execution approved or implied.

(handover to codex now)
