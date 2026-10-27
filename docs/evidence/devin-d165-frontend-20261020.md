# D165 — audit of D164 arm/generation comparability

Design-only self-audit of `devin-d164-frontend-20261020.md` §2/§3
against the D138/D140 evidence chain. One real inconsistency found;
amendment appended to D164 (original preserved).

## 1. The conflict — real

D164 §2 required "identical WS generation (pid+`gen` tag)" AND "each
arm a cold start". Both cannot hold: `d141_set_generation(pid, uuid)`
derives the tag from the WS pid — every cold start mints a different
pid ⇒ different `gen`. Literal reading makes the experiment
impossible; loose reading invites cross-generation record joins,
which the contract forbids.

## 2. Identifier stratification (what must match where)

| Identifier | Within an arm | Across arms |
|---|---|---|
| SkyLight image UUID (`96676A53…`) | identical — binary file identity | identical — same deployed image required |
| `gen` tag (pid-derived) | one value; all scored records share it | **necessarily differs** — new pid each cold start |
| services `tid` | one role-resolved tid | differs in value; role-identified by PASS_ENTER density, never by absolute value |
| probe msgid/payload (`0x74a7`) | constant | identical — same send fields |
| sender/port | constant | identical sender path required |
| instrumentation build | present+armed | identical — sentinel armed in BOTH arms |
| wait flag | constant | THE only difference between arms |

Comparison rule: arm-vs-arm at the **distribution level** (median
latency, cluster counts) — never record-level joins across
generations. Within-arm joins use §3 keys filtered by that arm's
`gen`.

## 3. Sampling constraints vs earlier design — supported

- ≥30 probes/arm: D138's stated floor (30 `0x74a7` per arm).
- Single fixed wait interval: D138 "one bounded wait variable" —
  the wait flag is binary off/on at one interval; sweeping intervals
  is out of scope.
- `max(8 ms, 0.5×interval)` clustering: D138's threshold rule,
  unchanged.
- Added rigor from the accepted contract: nonzero drops inside a
  probe window flag the arm (D164 §8) — unchanged.

## 4. Baseline controls (no invented numbers)

- Same SkyLight image UUID and same instrumented libmachook build in
  both arms — instrumentation present identically; only the wait
  flag differs.
- Same device/package state; no intervening installs between arms.
- Thermal/workload quiesced: no concurrent app launches, display
  activity held constant, ambient daemons unperturbed.
- Producer identity: the same QC composite producer path in both
  arms.
- Run-order noted; counterbalanced ordering (A/B/B/A-style) if
  repeat sets are taken — drift is a known confound the report must
  record, not eliminate by fiat.

## 5. Amendment applied to D164

Appended "D165 amendment" clarifying §2: "identical generation"
means identical SkyLight image UUID + identical instrumentation
build; the pid-derived `gen` tag differs per arm by construction.
Arm comparison is distribution-level only. Original scope preserved.
