# Frontend R030 result — independent bitstream retarget validation

Status: **validator delivered and tested; no backend R030 candidate yet; R029
candidate's target attribution corrected.** Read-only local work; no
device/source/package/Git change. Backend R030 not contacted or interrupted.

## Harness

`analysis/r030-independent-audit/` now contains:

- `validate_bitstream.py` — LLVM16-grammar recursive bitstream parser. It
  validates block declared lengths, abbreviations/records (including blob
  4-byte padding), wrapper declared payload and zero padding, module
  triple/data layout, and compares canonical decoded records. Only the module
  triple record and triple-derived STRTAB text may differ; the candidate
  triple must equal the exact target.
- `validate_container.py` — independent MTLB record/offset/hash/size checker.
- `run_tests.sh` — regression matrix (exit 0; all expectations met).

Matrix (`run-tests.log`):

```text
PASS positive             0
PASS attr_change          2   (1 attr-group diff, nofree added)
PASS body_change          2   (76 record diffs)
PASS param_change         2   (71 record diffs)
PASS wrong_triple         2   (same-length 14 vs 15, triple_match=false)
PASS trailing_garbage     2   (nonzero trailer)
PASS truncated_block      3   (blob out of bounds)
PASS r029_candidate       2
PASS converted_attr_diff  2   (408 diffs)
PASS container_exact_target 2
PASS container_structural   0
```

## Critical correction

The saved R029 `candidate-F11H.record.bc` never changed the module triple:

```text
original/candidate module triple = air64-apple-macosx13.4.0
converted module triple          = air64-apple-ios19.0.0-macabi
```

Its 12 changed bytes at 6,885-6,896 are a string-table entry, not the
`MODULE_CODE_TRIPLE` record. The validator reports `triple_match=false`, one
canonical STRTAB diff, container structure PASS, exact-target FAIL. This
corrects R029's target attribution and is consistent with the observed
`Target OS is incompatible`. The candidate is rejected as a retarget.

## Productization gates

Change the module triple via bitstream-aware re-encode; a raw string patch is
insufficient. If the exact 28-byte target differs in length from the source,
re-encode while restoring the original intrinsic declaration attribute
records. Require canonical equality of all non-triple records (attributes,
parameters, call sites, body, constants, metadata, calling conventions,
abbreviations/block lengths), exact target/profile/data layout, wrapper
payload/zero padding, and MTLB offsets/hashes. Fail closed on any unexpected
change. Whole-module replacement must prove all converter transforms are
preserved or reapply them; do not assume equivalence.

No backend R030 candidate/final no-hook replay exists at snapshot; no
compiler acceptance is claimed. Pending input is the exact candidate module/
MTLB/request and final no-diagnostic-hook acceptance.

Files:
- `docs/evidence/r030-retarget-independent-audit-20261018.md`
  (SHA-256 `f502e2f318d92cc68eb4debad716d1f70331a4933fd03a2697573df7a61c72fd`)
- raw harness/fixtures under `analysis/r030-independent-audit/`
  (`validate_bitstream.py`, `validate_container.py`, `run_tests.sh`,
  `run-tests.log`, `matrix.json`, `llvm16/sources.json`, fixtures).

No background tasks remain running or queued.

(handover to codex now)
