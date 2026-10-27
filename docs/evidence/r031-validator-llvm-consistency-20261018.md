# R031 — validator vs actual LLVM on saved R030 artifacts

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-031-to-frontend.md`. Read-only local audit; no
production/source/package/Git/device change. Disposable validator/fixtures
are under `analysis/r031-independent-audit/` (gitignored). Checkpoint:
`analysis/r031-independent-audit/CHECKPOINT.md`.

## 1. Inputs and host decoder

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| unmodified original `orig-F11H.bc` | 6,944 | `0fa8f789…8e2c` |
| old STRTAB-only `candidate-F11H.record.bc` | 6,944 | `2f2de66b…5b1b` |
| failed R030 bitstream candidate `retargeted-F11H.record.bc` | 6,960 | `e76b655c…7f1a` |
| converted `conv-F11H.bc` | 7,392 | `6aeb00477fa3…28f7` |

Host decoder: `Apple clang version 17.0.0 (clang-1700.6.4.2)`, invoked as
`xcrun clang -x ir -S -emit-llvm <file> -o /tmp/out.ll` (a real LLVM
bitcode reader; it overrides the module triple for this host, so its success
is grammar validity, not target-reader acceptance).

Exact host results (`host-decode-excerpts.txt`):

```text
original      exit 0  (warning: overriding the module target triple)
old STRTAB    exit 0  (warning: overriding the module target triple)
converted     exit 0  (warning: overriding the module target triple)
R030 backend  exit 1  error: can't enter sub-block: current code size is 0
abbrev-zero   exit 1  error: can't enter sub-block: current code size is 0
block-size    exit 1  error: can't skip to bit 21785252992 from 12864
```

The R030 candidate is therefore grammar-invalid despite its exact
`air64-apple-ios19.0.0-macabi` module triple and matching canonical records.

## 2. What the R030 validator missed

The previous R030 validator on the R030 candidate returned FAIL only because
it counted `MODULE_CODE_VSTOFFSET` (1466 -> 1469) as a semantic diff:

```text
R030 validator: triple=True, canonical_diffs=1 (VST offset), no structure check
```

That field is allowed to shift when the module grows. If it had been treated
as structural, the old validator would have accepted the candidate. It did
**not** detect the actual host grammar failure.

The missing scoping/length invariant is in the **value symbol table function
entries**. LLVM's reader uses each `VST_CODE_FNENTRY` offset to jump to a
function block; the source grammar is exact:

- `LLVMBitCodes.h`: `VST_CODE_FNENTRY = 3` (`[valueid, offset, name...]`).
- `BitcodeReader.cpp:2668-2681` (`jumpToValueSymbolTable`) jumps to
  `Offset*32` and requires a `VALUE_SYMTAB` subblock.
- `BitcodeReader.cpp:2684-2697` (`setDeferredFunctionInfo`) computes a
  function block bit offset from `Record[1]-1`.
- `BitcodeReader.cpp:4476-4484` subtracts one from `MODULE_CODE_VSTOFFSET`.
- `BitcodeWriter.cpp:1231-1251` and `3279-3288` backpatch the VST offset as
  `(VSTStart - bitcodeStart)/32 + 1`.

The R030 candidate inserted 96 bits (3 words) before the function blocks and
correctly updated the module VST offset record to 1469, but left the VST
function entries unchanged. Exact values:

```text
original function block starts: 35040, 35424, 43328
R030 candidate starts:         35136, 35520, 43424   (+96 bits)
VST FNENTRY offsets unchanged:  1093, 1105, 1352
expected offsets:              1096, 1108, 1355
module VST offset:             1469 (record and actual VST start agree)
```

The host reader jumps to the stale function positions, which are now before
the real `ENTER_SUBBLOCK` headers, and reports
`can't enter sub-block: current code size is 0`.

## 3. Improved disposable validator

`analysis/r031-independent-audit/validate_bitstream.py`
(SHA `baf021ca…76f1`) adds:

1. block header/body tracking with block paths;
2. validation that `MODULE_CODE_VSTOFFSET` equals the actual module-level
   `VALUE_SYMTAB` block position;
3. validation that every `VST_CODE_FNENTRY` offset resolves to an actual
   module-level function block;
4. structural fields excluded/normalized from semantic comparison:
   `MODULE_CODE_VSTOFFSET` and `VST_FNENTRY` offsets are checked structurally,
   not treated as semantic differences; triple-derived STRTAB text is
   normalized;
5. canonical record comparison is performed **only when structure is valid**
   (otherwise `canonical_compared=false`).

Required grammar failures remain fail-closed: invalid block code width,
declared block-length mismatch, blob/record bounds, nonzero trailing padding.

Matrix (`run-tests.log`, exit 0):

```text
PASS original_self            0
PASS old_strtab               2  (triple mismatch + STRTAB diff; structure valid)
PASS r030_backend             2  (structure invalid; canonical comparison skipped)
PASS converted                2  (407 semantic diffs; structure valid)
PASS positive                 0
PASS attr_change              2  (1 attr-group diff)
PASS body_change              2  (76 record diffs)
PASS param_change             2  (71 record diffs)
PASS wrong_triple             2
PASS trailing_garbage         2
PASS truncated_block          3
PASS abbrev_width_zero        3  (invalid code width 0)
PASS block_size_wrong         3  (block 8 length mismatch)
PASS r030_backend_structure_detail
```

R030 candidate result after the fix:

```text
triple_match=true
canonical_compared=false
structure_ok=false
vst_offset_record=1469, expected=1469
function_blocks=3
bad_entries: valueid 3 offset 1093 expected bit 35040
             valueid 4 offset 1105 expected bit 35424
             valueid 7 offset 1352 expected bit 43328
```

Thus the validator now reproduces the host rejection class without accepting
the candidate.

## 4. Structural vs semantic fields

Allowed to shift (and required to remain internally consistent):

- module/VST block word counts and block sizes;
- `MODULE_CODE_VSTOFFSET` (must equal actual VST block position);
- `VST_CODE_FNENTRY` function offsets (must resolve to actual function blocks);
- wrapper payload size, trailing zero padding, MTLB offsets/hashes.

Must remain semantic-equivalent after a valid structure:

- exact module triple `air64-apple-ios19.0.0-macabi` and data layout;
- intrinsic/non-intrinsic function, return, parameter and call-site attrs;
- body, constants, globals, aliases, comdats, calling conventions;
- metadata/module flags/debug info/attachments;
- VST valueids/names (offsets normalized) and non-structural records.

## 5. Acceptance criteria and unsupported grammar

An independent acceptance requires all of:

1. real LLVM decoder accepts the artifact (grammar validity), and the exact
   target reader accepts it (not merely the host decoder);
2. structural VST and function-offset checks pass;
3. canonical records match after the structural allowlist and exact triple;
4. wrapper payload/padding and MTLB offsets/hashes are consistent;
5. no unsupported abbreviation/block form is silently accepted; invalid
   code width, block length, blob bounds or VST scoping fails closed.

Host decode success is not target acceptance: the old STRTAB candidate and
converted module decode in host LLVM but fail the exact target triple or
attribute-equivalence gates. The R030 candidate is grammar-invalid even
though its parsed triple/canonical records looked attractive.

## 6. Pending / limits

- No stable R031 candidate is present under `analysis/r031*/` at snapshot;
  the backend R030 candidate is the failing artifact audited here. No polling
  or backend contact was performed.
- The host decoder is Apple clang 17, not the exact 22F82 target reader; it is
  used only as a grammar-consistency oracle. Target acceptance still requires
  the backend's final no-hook replay and real compiler reply.
- No production/source/package/Git/device change.

Raw evidence: `analysis/r031-independent-audit/`:
`validate_bitstream.py`, `run_tests.sh`, `run-tests.log`,
`run_host_decode.sh`, `run-host-decode.log`, `r030-backend-validation.json`,
`r030-validator-on-backend.json`, `host-decode-excerpts.txt`, fixtures and
`final-hashes.txt`.

(handover to codex now)
