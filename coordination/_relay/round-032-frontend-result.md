# Frontend R032 result — R031 candidate independent audit

Status: **one-module diagnostic candidate passes; production readiness is not
claimed.** Read-only local audit; no backend/source/package/Git/device change.
Backend R031 was not messaged or polled; its files were stable before/after
the audit.

## Results

- Stable artifacts: roundtrip-orig payload `ce018722…` (6,916 B);
  roundtrip-conv payload `042303be…` (7,372 B); candidate payload
  `8dc40d78…` (6,920 B); candidate record `5d805ec5…` (6,944 B); candidate
  MTLB `7b71a647…`; candidate type-15 `75663758…`. Before/after hashes are
  identical excluding `__pycache__`.
- Corrected validator:
  - roundtrip_orig PASS: canonical diffs 0, structure true, VST 1466/1466;
  - roundtrip_conv PASS: canonical diffs 0, structure true, VST 1568/1568;
  - candidate_payload PASS: exact triple, canonical diffs 0, structure true,
    VST 1467/1467;
  - candidate_record PASS: exact triple, canonical diffs 0, structure true,
    wrapper payload 6,920, 4 zero trailing bytes.
- Original/candidate PARAMATTR groups: 14 entries, no wire 71/62/63/61;
  original intrinsic attrs preserved. Roundtrip_conv has 16 entries with
  those codes, as expected from the materialization path.
- Host LLVM (`Apple clang 17.0.0`) decode: roundtrip_orig, roundtrip_conv,
  candidate_payload and candidate_record all exit 0 (target-triple override
  warning only). This is grammar validity, not target-reader acceptance.
- MTLB container PASS: 1,825 records, module SHA `5d805ec5…` equals the
  record, exact triple, trailing zero, bitcode delta -448.
- Type-15 request: prefix identical except six size fields, module at 0xF0
  equals the record, module size 6,944, total 7,184, wrapper bcsize 6,920.
- Writer review: fail-closed checks exist for wrapper truncation, header,
  block length/END, abbrev bounds/encodings, array/blob bounds, one module/VST,
  FNENTRY count, value/patch bounds, block-size arithmetic, two-pass stability
  and reparse/audit. Hardening items: reject block code width 0/>32 explicitly;
  require zero wrapper trailing padding; require exact approved target profile
  (not only length 28); optionally assert block-length equality in audit.

## Boundary

This is **one-module diagnostic success**: F11H is canonically re-encoded and
inserted into the converted MTLB; the other 1,824 modules are unchanged. It
does not prove the exact target reader accepts the artifact, does not prove
full-converter/all-module integration, and makes no compiler/pixel claim.

Production readiness still requires final no-hook target-reader replay and a
real reply, canonical writer integration/profile gating across all target
modules, the hardening items plus regression tests, and reconciled final
hashes/provenance. Backend R031 final handover is pending; any changed
artifact needs a fresh audit.

Files:
- `docs/evidence/r032-r031-candidate-audit-20261018.md`
  (SHA-256 `969eb1990953bb146e25b39908441985e411b2bdf7597af6052ac215f90885bd`)
- raw evidence under `analysis/r032-independent-audit/` (validator, JSON
  results, container/request checks, host decode log, snapshots, summary).

No background tasks remain running or queued.

(handover to codex now)
