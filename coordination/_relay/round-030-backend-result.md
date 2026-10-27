# Backend R030 result — exact-profile bitstream retarget (fail-closed)

One disposable bitstream-aware retargeter under `analysis/r030-f11h`;
one read-only LLVM16 decode of the disposable candidate. No private A/B
was run because the strict local invariant failed. No production
compiler/WS/generation/package/source/Git/policy/marker change.

## Implemented retarget

The original F11H triple (unabbreviated `MODULE_CODE_TRIPLE`, 24 VBR6
chars at bits 12,800–13,103) was re-emitted with the exact 28-byte
`air64-apple-ios19.0.0-macabi`. To preserve every following 32-bit block
alignment, the record uses eight redundant zero VBR6 chunks split between
the record-code and element-count fields: LLVM decodes the same values
(2, 28) and the growth is exactly 96 bits (3 words). Every other bit is
copied verbatim.

Result: triple record 303→399 bits; VST word offset 1,466→1,469; module
word count 1,465→1,468; payload 6,916→6,928; wrapper 6,948; record 6,960
(`e76b655c…7f1a`). Independent audit: 17 blocks with identical widths/
word counts except the module count; 26 records differing only in the
triple, VST offset and the STRTAB blob's absolute position; blob content,
PARAMATTR groups (14; group 4 `[argmemonly,nounwind]`, no ID71), body,
metadata, constants and ABI otherwise unchanged.

## Failed strict invariant

LLVM16 `llvm-dis` on the disposable candidate:

```
error: can't enter sub-block: current code size is 0
exit 1
```

Host clang gives the same. A canonical 48-bit-growth variant failed
earlier with `Malformed global initializer set`. The decoded records and
block widths match the original, but LLVM's reader rejects the spliced
block/VST/END structure; the remaining divergence is in writer-level
block/VST length and abbreviation semantics (or the redundant VBR chunks),
not in any decoded attribute/body/metadata.

**No A/B was authorized or run.** No compiler service, WindowServer or
generation was started. The R028 provisional 24-byte result (`Target OS is
incompatible`) remains the last compiler evidence and is not re-tested.

## Next artifact

A full bitstream writer/re-encoder that re-emits the module, VST,
SYMTAB/STRTAB block lengths and END markers with LLVM writer semantics,
preserves the original intrinsic attribute groups (no llvm-dis/as
round-trip that reintroduces them, no attribute removal), and decodes
under LLVM16 with only the triple and structural length/offset changes.
Then the bounded direct control/candidate replay and, on real success, the
no-diagnostic-hook endpoint confirmation. Do not patch the reader, strip
attributes or send the current artifact.

## Cleanup and state

Device temp moved to
`/var/mobile/macws_r030_evidence_20261018/temp` (candidate, empty stdout,
LLVM16 error); host copy under `analysis/r030-f11h/device-evidence/temp/`.
No owned process; production compiler count 14; native SpringBoard 872 /
tccd 883 / backboardd 905; package 0.3.4; provider and both libmachook
hashes unchanged; all six markers absent. No new trustcache additions;
R028 entries remain as recorded.

Evidence: `docs/evidence/r030-exact-profile-retarget-20261018.md`,
`analysis/r030-f11h/`, device
`/var/mobile/macws_r030_evidence_20261018/`.

(handover to codex now)
