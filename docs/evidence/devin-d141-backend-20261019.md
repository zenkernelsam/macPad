# D141 — isolated svctime record prototype: implemented + tested

**Task**: `coordination/_relay/devin-141-backend-task.md` — minimum
service-half diagnostic prototype from D140, under
`analysis/d141-svctime-candidate/` only. No production/device changes.

## Prior-art inspection

- D140 boundary map (`docs/evidence/devin-d140-frontend-20261019.md`):
  six `pacibsp` entry sites on SkyLight UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`,
  three unsafe mid-function edges proxied by the existing `mach_msg*` interposes.
- D107/D109 pace candidate (`analysis/d107-pacing-candidate/`): pinned
  `CLOCK_MONOTONIC` domain, seq/pid/tid identity, bounded trace discipline
  — reconciled by carrying the same clock and per-record identity.
- Nothing unsafe copied: the pace candidate's *line-oriented fd trace* was
  replaced here by fixed-size binary records; its arm-check pattern
  (single init-time sentinel probe) is adopted verbatim in spirit.

## Delivered (`analysis/d141-svctime-candidate/`)

- `d141_svctime.h` / `d141_svctime.c` — 64-byte fixed records, nine edge
  kinds (6 entry wrappers + MACH_CALL/RET legs), 256-slot bounded
  nonblocking MPSC ring (CAS claim, drop-and-count on full, never
  overwrite), init-time-only sentinel gate, `(pid<<32)^uuid_low64`
  generation tag, errno save/restore, wrapper brackets with no state held
  across `fn`.
- `d141_svctime_test.c` — T1–T8 deterministic tests.
- `run_tests.sh`, `README.md` with contract invariants + honesty bounds.

## Results

- `run_tests.sh`: **354 checks, 0 failures** under ASan+UBSan —
  ordering/correlation, exact saturation drop accounting, errno/return
  preservation, nested-pass reentrancy, zero output disarmed, post-hoc arm
  refused, MPSC conservation (`emitted+dropped == N` exactly).
- `clang -arch arm64 -c d141_svctime.c` → `Mach-O 64-bit object arm64` —
  a **real arm64 compile**; compile-level claim only (no arm64 execution
  on this host, none claimed).

## Boundaries kept

No production `libmachook`/permit-table/package edits; no build of the
product; no device access, sentinel, deploy, or GUI interaction. The
prototype never patches anything — orig is a caller-supplied function
pointer, matching the trampoline call contract.

## Documented non-claims

Kernel-enqueue timestamps invisible; drain is a bounded snapshot
(claimed-uncommitted slots counted via `d141_skipped`); correlation ≠
causality; pass brackets are not per-message (aux = handled-count per
D140's requirement).

Candidate **PENDING**.

(handover to codex now)
