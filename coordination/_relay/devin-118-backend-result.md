# devin-118 backend result — fd-mutation phase + quiesced predicate fixed

Task: `coordination/_relay/devin-118-backend-task.md`
Evidence: `docs/evidence/devin-d118-backend-20261019.md`
Touched: `analysis/d113-trace-transport/` only.

## Fixes

- **`D113_FDMUT` (bit60) added to `adm`** under `D113_PHASE`.
  `fdmut_claim()` CASes the count-check and the claim in ONE atomic
  step (`-4` if any drainer admitted or phase held); mutators hold it
  through all close/open/publish steps and release on every path.
  Drainer `admit()` refuses while `FDMUT` is held — a drainer either
  admits before the claim (mutator sees `DR_MSK` → `-4`) or during it
  (admit sees `PHASE` → `-1`); no third interleaving. The Codex-
  reproduced window (pause after check → drainer admits → close → fd
  reuse) is closed for both `drain_close` and `drain_open`.
- **`d113_quiesced()`** now returns `CLOSED && !(CNT | PHASE)` — false
  while `FINAL`, `FREEING`, or `FDMUT` is active.
- New test hooks 7 (close, FDMUT held pre-close) and 8 (open, FDMUT
  held pre-mutate), test builds only.

## Deterministic tests (ASan)

- **L13** = exact reproduced interleaving: close paused mid-mutation →
  drainer refused -1 → close completes 0 → `fileA=0B fileD=0B em=0`
  (replacement fd-reuse target stays empty; nothing misdirected).
- **L14** = open/rebind interleaving: drainer refused while FDMUT held;
  reopen completes → records land in new fileB, fileA=0B.
- **L9** extended: `d113_quiesced()==0` asserted while `FINAL` held.
- L1–L12 all still PASS.

## Regressions

- C0–C12 all PASS; D112 reference 16/16, negative adapter 10 expected
  FAILs, prototype 16/16; `git diff --check` exit 0.

Note: producers also refuse during the bounded `FDMUT` window (drops
counted, contract-correct). Candidate **PENDING**.

(handover to codex now)
