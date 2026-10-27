# R030 — independent bitstream retarget validator and audit

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-030-to-frontend.md`. Read-only local work: no
production/source/package/Git/device change. Disposable validator and
fixtures are under `analysis/r030-independent-audit/` (gitignored). Backend
R030 has no saved bitstream-aware candidate at snapshot time; this audit
delivers the executable validator and audits the stable R029 candidate.

## 1. Authoritative grammar

The validator uses the LLVM 16.0.0 release tag `llvmorg-16.0.0`, commit
`ca289b8aaad5e220a66c7afb25387b9d01509b1c`:

- `BitstreamReader.cpp` SHA `8c8fa712…87f9f4`
- `BitstreamWriter.h` SHA `896b6709…63125`
- `BitCodes.h` SHA `763f2d2b…25147a`
- `BitCodeEnums.h` SHA `4f6c75f9…fddb`
- `LLVMBitCodes.h` SHA `41c9d380…0912`

It validates every `ENTER_SUBBLOCK`/`END_BLOCK` declared length, every
abbreviation/record bound (including 4-byte padding for blobs), module triple
and data-layout records, wrapper declared payload, and zero trailing padding.
It then compares **canonical decoded records**; only the module triple record
and triple substrings in the string table are allowed to differ. The
candidate triple must equal `--target-triple` exactly.

## 2. Executable harness and matrix

`validate_bitstream.py` (SHA `6b2a6a6b…cf8d6`) and
`validate_container.py` (SHA `39067a95…5e74`) are independent of the builder.
`run_tests.sh` exits 0 and produced `run-tests.log`
(SHA `384b0965…f04a`):

```text
PASS positive             exit=0
PASS attr_change          exit=2
PASS body_change          exit=2
PASS param_change         exit=2
PASS wrong_triple         exit=2
PASS trailing_garbage     exit=2
PASS truncated_block      exit=3
PASS r029_candidate       exit=2
PASS converted_attr_diff  exit=2
PASS container_exact_target exit=2
PASS container_structural exit=0
```

Negative fixtures (all local, under `analysis/r030-independent-audit/`):

| Fixture | Observed result |
| --- | --- |
| `attr_change` | exactly 1 canonical diff: PARAMATTR_GROUP record adds `0,62` (`nofree`) |
| `body_change` | 76 canonical differences (function/body/constant records) |
| `param_change` | 71 canonical differences (parameter/record tables) |
| `wrong_triple` | same-length 14.0 vs 15.0 triple: `triple_match=false`, canonical diff 0 |
| `trailing_garbage` | `trailing_zero=false` |
| `truncated_block` | parser rejects: `blob out of bounds` |
| converted vs original | 408 differences (materialized attrs/triple/reserialization) |

The fixtures are compiled by the local host clang; the data-layout negative
fixture was not retained because host clang rewrites the IR data layout. The
validator still compares data layouts and the positive/empty-candidate cases
exercise that gate.

## 3. R029 candidate correction — the module triple was never changed

The saved `candidate-F11H.record.bc` (6,944 B, SHA `2f2de66b…5b1b`) is not a
triple retarget:

```text
original module triple:  air64-apple-macosx13.4.0
candidate module triple: air64-apple-macosx13.4.0
converted module triple: air64-apple-ios19.0.0-macabi
```

The 12 bytes changed by R029 at offsets 6,885-6,896 are a **STRTAB string**,
not the `MODULE_CODE_TRIPLE` record. The validator reports:

```text
r029_candidate: triple_match=false
canonical_differences_excluding_triple=1  (the changed STRTAB entry)
container_structural: PASS (offsets/hashes/sizes valid)
container_exact_target: FAIL (module triple is still macosx13.4.0)
```

This corrects R029's target attribution: the builder's `AIR_TARGET_BYTES`
match was a string-table occurrence, and the candidate never achieved the
intended target triple. The observed `Target OS is incompatible` reply is
consistent with this unchanged module triple. The R029 structural/container
invariants remain valid, but the candidate is rejected as a retarget.

## 4. Wrapper/payload and semantic checks

- Original and candidate wrappers declare `bcsize=6916`; the F11H record is
  6,944 B = 20 B wrapper + 6,916 B payload + 8 zero trailing bytes.
- The validator checks the declared payload, trailing-zero requirement and
  exact target; a nonzero trailer or truncated block fails.
- For an exact-profile candidate, all canonical records other than the module
  triple must match the original after normalizing triple substrings in the
  string table. This covers intrinsic/non-intrinsic/parameter/call-site
  attributes, body, constants, metadata, calling conventions and block
  structure.
- MTLB container validation independently checks module offsets, MDSZ, HASH,
  bitcode section sizes and header fields. The R029 container is structurally
  valid; its module triple is the blocker.

## 5. Productization criteria

1. Change the **module triple record** via a bitstream-aware re-encode; a raw
   string patch is not sufficient. The exact target is
   `air64-apple-ios19.0.0-macabi` (28 bytes).
2. If the triple length changes, use a bitstream-aware re-encode that restores
   the original intrinsic declaration attribute records; do not choose an
   arbitrary same-length string.
3. Require all canonical records except the module triple (and triple-derived
   string-table text) to match the original: attributes, parameters,
   call-site attributes, body, constants, metadata, calling conventions,
   abbreviations and block lengths.
4. Preserve the wrapper's declared payload and zero padding; validate the
   MTLB offsets/hashes/sizes if a container is produced.
5. Gate on the exact profile/triple/data layout and fail closed on any
   unexpected attribute or body change.
6. Do not claim compiler acceptance without the final no-hook backend replay
   and a real reply; a diagnostic-hook run is not acceptance.

A whole-module replacement is not automatically conservative: it may discard
converter transforms applied to F11H. The validator's strict canonical
comparison is the guard: for the current orig/conv F11H text the body/
metadata are identical aside from attributes/triple, but a productized
candidate must prove that for the exact profile or reapply all transforms.

## 6. Pending / limits

- No backend R030 bitstream-aware candidate, final MTLB/request, or no-hook
  replay exists at snapshot. The validator is ready for that artifact; no
  compiler success is claimed.
- The validator is independent of the builder but is a minimal record-level
  decoder, not a full LLVM verifier. It is cross-checked against the exact
  LLVM16 grammar and host decode; final acceptance still requires the exact
  target reader.
- No device/source/package/Git change; no attribute removal or workaround.

Raw evidence: `analysis/r030-independent-audit/`:
`validate_bitstream.py`, `validate_container.py`, `run_tests.sh`,
`run-tests.log`, `matrix.json` (SHA `95dccee8…158a`), `llvm16/sources.json`
(SHA `041d852a…259e`), fixtures and `final-hashes.txt`.

(handover to codex now)
