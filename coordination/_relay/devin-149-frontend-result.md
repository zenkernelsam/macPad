# D149 frontend result — 2026-10-20

Deterministic follow-up test/design packet ready:
`docs/evidence/devin-d149-frontend-20261020.md`.

## Contents delivered

- **Item 1** — gate-loses-to-set_generation: preferred fix = re-CAS on
  UNINIT inside the bounded spin (fallback = documented -1-retry);
  deterministic schedule needs a new `d141_hook_gen_init` hook invoked
  while `set_generation` holds INITIALIZING; invariant = `gate_rc==1`,
  coherent gen publication.
- **Item 2** — parked claim + c+CAP: the same-slot overwrite is
  *unreachable by design*; the refusal IS the bound-check drop —
  schedule fills exactly to head−tail==CAP then asserts `dropped==1` on
  the c+CAP attempt + full claim-order drain after release.
- **Item 3** — ≥2 live producers overlapping a drain: `go`-barrier,
  drain loop starts before release, per-record `aux==f(msgid)` pattern
  + magic + unique ids (full-field tear detection), exact conservation.
- **Item 4** — post-call PASS_EXIT aux: proposed `aux_after(ret,ctx)`
  (or post-fn state read); test uses a value unknowable pre-call;
  README's unbacked handled-count claim flagged for sync.
- **Item 5** — mach legs: distinct-id join test, timeout RET(msgid=0)
  exclusion, nested LIFO pairing rule; reply join `id−100` documented as
  decoder-side.
- **Item 6** — verbatim-able contract text for D1/C3/D8/D9/D10.
- **Impossible-schedule table** — five schedules that cannot exist
  under bounded-drop semantics, each with the substitute assertion.
- **Proposed D150 backend task body** + 8-row per-test acceptance
  checklist.

No edits to `analysis/d141-svctime-candidate/`, no tests run, no
backend contact, no device. Milestone reminder: commit only after the
reviewer accepts a concrete backend change; none required here.

(handover to codex now)
