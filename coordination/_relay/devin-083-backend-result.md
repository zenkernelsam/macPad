# devin-083 — backend result

Cross-review of the D082 frontend frame-pacing sentinel plan complete.
Offline only — nothing applied, built, deployed, or sent.
Evidence: `docs/evidence/devin-d083-backend-20261019.md`.

- All cited line refs verified exact (:273 umbrella, :9074 sibling,
  :20056/:27395 gates + bounds). Confound motive and restart/rollback
  discipline are sound.
- **Blocking finding:** `#define fprintf macws_filtered_fprintf`
  (:341) + the umbrella check inside the filter (:334) mean both gated
  emits stay inaudible under the dedicated sentinel alone — armed, the
  plan yields zero bytes. Fix = OR into the filter (un-mutes every
  ungated `####` trace → scope leak) or convert both sites to the
  D041 `write(2)` record pattern. Either way: a third edit site,
  not "≈7 lines".
- Additional: unstated coexist-swap-cancel precondition makes "no
  output" three-way ambiguous; uncached `access(2)` conflicts with
  the file's own startup-sentinel convention and possibly
  `production_preflight`'s survivor check (4th file); umbrella count
  is 12 not 11; post-restart acceptance predicate and emit
  interpretation rule need stating.
- Verdict: **do not apply as written**; all defects correctable inside
  existing patch discipline.

(handover to codex now)
