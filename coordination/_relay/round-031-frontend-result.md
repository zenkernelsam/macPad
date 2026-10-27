# Frontend R031 result — validator vs actual LLVM

Status: **completed. The R030 validator did not detect the real grammar
failure; R031 now rejects the failed R030 candidate for the correct
VST-scoping reason.** Read-only local work; no production/source/package/Git/
device change. Checkpoint:
`analysis/r031-independent-audit/CHECKPOINT.md`.

## Host LLVM vs artifacts

`Apple clang version 17.0.0` (`xcrun clang -x ir -S -emit-llvm`):

```text
original      exit 0
old STRTAB    exit 0
converted     exit 0
R030 backend  exit 1  error: can't enter sub-block: current code size is 0
abbrev-zero   exit 1  error: can't enter sub-block: current code size is 0
block-size    exit 1  error: can't skip to bit 21785252992 from 12864
```

Host success is grammar-only; old STRTAB and converted still fail the exact
triple/attribute gates. No target-reader acceptance is claimed.

## Missed invariant and fix

The old R030 validator returned FAIL on the R030 backend module only because
it counted `MODULE_CODE_VSTOFFSET` (1466→1469) as a semantic diff. That field
is structural and may shift. It would have accepted the module after that
normalization, while host LLVM rejects it.

Root cause: the triple record grew by 96 bits (3 words), shifting function
block body starts from `35040/35424/43328` to `35136/35520/43424`, but the
VST `VST_CODE_FNENTRY` offsets stayed `1093/1105/1352` (expected
`1096/1108/1355`). Host LLVM jumps to the stale function positions and
enters a sub-block with code size 0. The module VST offset record 1469 is
correct; the function-entry offsets are not.

R031 `validate_bitstream.py` now:
- validates module VST offset against the actual module-level VST block;
- validates every VST function entry against actual function block positions;
- treats VST offset/function offsets as structural (validated, not semantic);
- compares canonical semantic records only after structure is valid;
- fails closed on invalid code width, block-size mismatch, blob/truncation
  and nonzero trailing padding.

R030 backend result after fix: `triple_match=true`,
`canonical_compared=false`, `structure_ok=false`, VST offset 1469/1469,
three bad function offsets (35040/35424/43328 expected). This matches the
host grammar failure and the candidate remains rejected.

## Matrix

`run_tests.sh` exit 0: original self PASS; old STRTAB FAIL (triple+STRTAB);
R030 backend FAIL (structure); converted FAIL (407 attrs);
positive PASS; attr/body/param/wrong-triple/trailing FAIL; truncated/abbrev-
width/block-size FAIL with exit 3. Host decode matrix is in
`run-host-decode.log` and `host-decode-excerpts.txt`.

## Acceptance criteria

Require real LLVM grammar acceptance **and** exact target-reader acceptance,
VST/function-offset structural consistency, exact triple/profile/data layout,
canonical semantic equality after the structural allowlist,
attributes/body/constants/metadata/calling conventions, wrapper payload/zero
padding fit, and MTLB offsets/hashes. Unsupported grammar fails closed; do
not relax rules to fit a candidate.

No stable R031 candidate was present without polling; pending input is the
backend final bitstream-aware candidate and no-hook replay.

Files:
- `docs/evidence/r031-validator-llvm-consistency-20261018.md`
  (SHA-256 `81d4587c4b019550afb895ebecee60565087c2070bc53fece98092ffc46a07c3`)
- raw evidence under `analysis/r031-independent-audit/` (validator, matrices,
  host decode excerpts, failed R030 candidate validation, fixtures).

No background tasks remain running or queued.

(handover to codex now)
