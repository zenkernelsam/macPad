# R032 — independent audit of the stable R031 candidate

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-032-to-frontend.md`. Read-only local audit; no
production/source/package/Git/device change and no backend file changes.
Disposable audit files are under `analysis/r032-independent-audit/`.
Backend R031 was still Working and was not messaged or polled.

## 1. Stable artifacts

Before/after snapshots of `analysis/r031-f11h/` are identical (excluding the
existing `__pycache__`):

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| `roundtrip-orig.payload.bc` | 6,916 | `ce018722…ec9b` |
| `roundtrip-conv.payload.bc` | 7,372 | `042303be…b849` |
| `retargeted-F11H.payload.bc` | 6,920 | `8dc40d78…c269` |
| `r031-candidate.record` | 6,944 | `5d805ec5…52cb` |
| `mpsimage.r031-candidate.metallib` | 17,293,876 | `7b71a647…03aae` |
| `r031-candidate.type15.data` | 7,184 | `75663758…da1e` |

The candidate wrapper declares 6,920 payload bytes and has 4 zero trailing
padding bytes. The MTLB embeds a module with the exact record SHA
`5d805ec5…52cb`; its bitcode section is 448 bytes smaller than converted,
and all 1,825 function records remain structurally valid.

## 2. Corrected validator results

`analysis/r032-independent-audit/validate_bitstream.py`
(SHA `baf021ca…76f1`) includes the R031 VST invariants and structure-gated
canonical comparison:

```text
roundtrip_orig   PASS  canonical_diffs=0  structure=true  VST 1466/1466
roundtrip_conv   PASS  canonical_diffs=0  structure=true  VST 1568/1568
candidate_payload PASS canonical_diffs=0  structure=true  VST 1467/1467
candidate_record  PASS canonical_diffs=0  structure=true  VST 1467/1467
```

Candidate details:

- module triple: `air64-apple-ios19.0.0-macabi` (exact target);
- data layout equal to original;
- wrapper payload 6,920, trailing 4 zero bytes;
- original/candidate PARAMATTR_GROUP records: 14 entries, no wire
  `71/62/63/61`; the original intrinsic declaration attrs are preserved.
  The converted roundtrip has 16 entries and includes 71/62/63/61, as
  expected from the LLVM16 materialization path;
- VST module offset and all three function entries resolve to actual
  positions; no bad entries.

## 3. Actual host LLVM decode

Host: `Apple clang version 17.0.0 (clang-1700.6.4.2)`, command
`xcrun clang -x ir -S -emit-llvm <file> -o /tmp/out.ll`:

```text
roundtrip_orig   exit 0  (warning: overriding module target triple)
roundtrip_conv   exit 0  (same warning)
candidate_payload exit 0 (same warning)
candidate_record  exit 0 (same warning)
```

This is grammar validity only; the host overrides the module triple, so it
does not prove exact target-reader acceptance.

## 4. Container and request checks

`validate_container.py` (SHA `4935a8e2…bf795`) on the R031 MTLB:

```text
verdict PASS
records 1825
module_bytes 6944
module_sha256 5d805ec5…52cb
module_triple air64-apple-ios19.0.0-macabi
converted_bitcode_size 14739456
candidate_bitcode_size 14739008  (delta -448)
```

Type-15 request (`r031-candidate.type15.data`):

```text
prefix same as control except the six size fields
module at 0xF0 equals r031-candidate.record exactly
module size 6944; total 7184; wrapper bcsize 6920
```

## 5. Writer review (`analysis/r031-f11h/bcrewrite.py`)

The disposable canonical writer has substantial fail-closed behavior:

- parser checks wrapper truncation, header magic, exactly one module, block
  length/END arithmetic, abbreviation IDs/encodings, array/blob bounds,
  exactly one module VST, FNENTRY count vs function blocks, and trailing
  top-level bits (`Parser.parse`, `parse_entries`);
- writer checks value fit, patch bounds, block size arithmetic, field count,
  VST/VSTOFFSET capture, FNENTRY abbrev shape, two-pass stability and
  re-parse/audit equality (`Writer`, `main`);
- the record audit compares block IDs/widths, abbreviation definitions,
  record codes/fields, and allows only triple, module VST offset and
  module-level FNENTRY word offsets (`audit_module`).

Actionable hardening items (not defects in this candidate):

1. `Parser.parse_entries` should explicitly reject block code width 0 or
   greater than 32; a zero-width malformed block can be parsed as an
   immediate END and only be caught indirectly by length/audit.
2. `parse_payload_file` should require wrapper trailing padding to be zero;
   the independent validator checks this, but the writer parser currently
   ignores trailing bytes.
3. `apply_triple` checks only that the target is 28 bytes; production must
   require the exact approved profile string, not merely length.
4. `audit_module` compares block id/width/entries but not the block length
   field explicitly; parser/writer/reparse checks cover lengths, but an
   explicit equality assertion would tighten the gate.

No unsupported grammar fallback or attribute stripping was found.

## 6. Diagnostic success vs production readiness

**One-module diagnostic success:** the R031 F11H candidate and generated
MTLB/request pass the corrected independent validator, canonical semantic
comparison, VST structure, wrapper/container checks, and host LLVM grammar
decode. This is still only one module inserted into a converted container;
it does not prove the full converter or the exact target reader.

**Production patch readiness is not claimed.** It requires at least:

- final backend no-diagnostic-hook replay on the exact target reader and a
  real compiler reply;
- applying the canonical writer across all target modules/profile, not just
  F11H, with exact triple/profile gating;
- the four writer hardening items above and focused regression tests;
- final MTLB/request hashes and provenance reconciled;
- separate acceptance of pixels/compiler reply as applicable.

## 7. Pending / limits

- Backend R031 has not handed over; its files remained stable during the
  audit (before/after hashes identical excluding `__pycache__`). Any future
  change requires a fresh audit.
- Host clang 17 is a grammar oracle only; the exact 22F82 target reader was
  not run locally (no device).
- No compiler success or production deployment is claimed.

Raw evidence: `analysis/r032-independent-audit/`:
`validate_bitstream.py`, `validate_container.py`, `candidate_*.json`,
`roundtrip_*.json`, `container-r031.json`, `request-r031.json`,
`host-decode-r031.txt`, `audit-summary.json`, `r031-snapshot-*.txt`,
`final-hashes.txt`.

(handover to codex now)
