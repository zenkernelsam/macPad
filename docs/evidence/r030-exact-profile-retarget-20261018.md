# R030 — exact-profile bitstream retarget (fail-closed before A/B)

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-030-to-backend.md`. Disposable bitstream-aware
retargeter under `analysis/r030-f11h` only. No production
compiler/WS/generation/package/source/Git/policy/marker change. One
read-only LLVM16 decode of the disposable candidate; **no private-endpoint
A/B was run** because the strict local invariants failed.

## 1. Result

The original F11H triple was re-encoded at the bitstream level to the exact
28-byte profile `air64-apple-ios19.0.0-macabi`, preserving every decoded
record, attribute group, function body, constant, metadata and ABI. The
independent record audit passes. However, LLVM 16 cannot decode the
spliced module:

```
/var/jb/usr/lib/llvm-16/bin/llvm-dis: error:
  can't enter sub-block: current code size is 0
exit 1
```

Host clang (LLVM 17) rejects the same module with the same error. The
strict invariant "LLVM16 can decode the candidate and only the triple and
structural lengths differ" therefore **does not hold**; the packet's A/B
gate is not satisfied, so no compiler request was sent to the owned
endpoint.

## 2. Inputs

- Original F11H record 6,944 bytes (`0fa8f789…`): 20-byte wrapper,
  6,916-byte payload (`ce018722…`), 8 zero padding.
- Converted/request module: 7,372 payload `042303be…` (R028 provenance).
- Frontend R029 review: accept structural invariants only; the 24-byte
  candidate is rejected as a working compiler submission; exact profile
  required; no-hook acceptance required for any success.
- Attribute wire (frontend R027): ID71 = `nocallback`; original wire
  group 4 `[argmemonly,nounwind]`; converted adds
  `nocallback(71), nofree(62), nosync(63), willreturn(61)`.

## 3. Bitstream retargeter

`analysis/r030-f11h/retarget_bitstream.py` (SHA-256
`ecd9655a39610dd6c0b86883d9c5ec83ad79160d4fb65c756975d26065127803`):

- The original `MODULE_CODE_TRIPLE` is an **unabbreviated** record at bit
  range 12,800–13,103: record code 3 (module block code width 3),
  `MODULE_CODE_TRIPLE = 2`, 24 VBR6 ASCII chars.
- The triple is re-emitted as record code 2 with the exact 28-char target
  and 28 VBR6 chars. The canonical record growth is 48 bits.
- To preserve every following 32-bit block alignment, the record carries
  eight redundant zero VBR6 continuation chunks: six in the record-code
  field (7 chunks total) and two in the element-count field (3 chunks
  total). LLVM's VBR6 decoder returns the same values (2, 28); the record
  growth is exactly **96 bits (3 words)**. No record/attribute value
  changes.
- The VST block remains 32-bit aligned; its fixed32 word offset is updated
  from 1,466 to 1,469. The module block word count is updated 1,465 →
  1,468. The VST block, all function/metadata blocks, the SYMTAB/STRTAB
  blocks and every other bit are copied verbatim.
- Resulting wrapper payload 6,928 bytes (from 6,916); wrapper 6,948;
  record padded to 6,960 bytes (was 6,944). Record SHA-256
  `e76b655ce075c189b55e6676d64c90ff363cbfdc4f678561e59d365c0d337f1a`;
  payload SHA-256 `afe3fbf4a21bb3fbcc6e2b0b58d39de1d692ef56efa020198268fc24a314c129`.

`retarget-summary.json` records all positions/deltas
(`triple_record_old_bits=303`, `new_bits=399`, `delta_bits=96`,
`vst_offset=1466→1469`, `module_words=1465→1468`,
`payload=6916→6928`, `record_padding=12`).

## 4. Independent structural audit

The independent module/STRTAB parser (`inspect_records2.py`, SHA-256
`20f504e4…ff56`) on the retargeted record:

- 17 block entries; all block IDs, code widths and word counts are
  **identical to the original except the module block's word count
  (1,465 → 1,468)**.
- 26 parsed module/identification/STRTAB records; the normalized values
  differ only in:
  - the triple record (24 → 28 chars, exact target);
  - the VST offset record (1,466 → 1,469);
  - the STRTAB blob's absolute byte position (6,440 → 6,452) caused by
    the 12-byte payload growth; the blob **content** (470 bytes) is
    unchanged.
- The PARAMATTR_GROUP block is before the triple and is copied verbatim:
  14 groups, group 4 `[argmemonly(45),nounwind(18)]`,
  group 7 `[45,43,18,21]`; **wire ID71 count = 0**.
- The datalayout, source filename, section names, global/function records,
  function/metadata blocks, VST/SYMTAB/STRTAB content and all other
  records are value-identical to the original.

This is the strongest structural result achieved: only the triple, the
structural VST word offset and the blob's absolute position differ.

## 5. Failed strict invariant

The bitstream-aware splice is **not accepted by LLVM**:

- LLVM16 `llvm-dis` on the disposable candidate: exit 1,
  `error: can't enter sub-block: current code size is 0`.
- Host clang (LLVM 17): same error.
- An earlier canonical 48-bit-growth variant failed differently
  (`Malformed global initializer set`), confirming that merely lengthening
  the triple without correct block alignment is invalid.
- The independent parser and LLVM's reader agree on every block width and
  record value up to the triple/VST, but LLVM's reader still fails; the
  remaining divergence is in the block/VST/END/abbreviation structural
  semantics after the record-length change (or the redundant VBR chunks
  in the record fields), not in the decoded records.
- The exact missing invariant: a retargeter that re-emits the **module
  block, VST and block lengths with LLVM's writer semantics**, rather than
  splicing bits and relying on redundant VBR chunks. The current artifact
  must not be sent to the compiler or used as an A/B candidate.

Because the strict invariant failed, the packet's authorized A/B was
**not run**. No compiler service, WindowServer or generation was started.
The R028 provisional 24-byte result (`Target OS is incompatible`) remains
the last compiler evidence; it is not re-tested here.

## 6. Next artifact

A full bitstream writer/re-encoder that:

1. parses the module into records/blocks with their abbreviation scopes;
2. re-emits the triple with the exact 28-byte profile;
3. re-emits the module/VST/SYMTAB/STRTAB block lengths, END markers and
   VST offset with LLVM writer semantics (no redundant VBR padding);
4. preserves the original intrinsic attribute groups (no llvm-dis/as
   round-trip that reintroduces them, no attribute removal);
5. validates with LLVM16 `llvm-dis` and an independent record audit, with
   only the triple and structural length/offset changes allowed.

After that passes, the packet's one bounded direct converted-control vs
candidate replay and, on real success, the no-diagnostic-hook endpoint
confirmation become authorized. Do not patch the reader, strip attributes
or send the current artifact.

## 7. Cleanup and state

- Device temp `/var/mobile/macws_r030_temp` moved to
  `/var/mobile/macws_r030_evidence_20261018/temp` (retargeted record,
  empty stdout, LLVM16 error). Host copy under
  `analysis/r030-f11h/device-evidence/temp/`.
- No owned process remains; production compiler count 14; native
  SpringBoard 872 / tccd 883 / backboardd 905 unchanged; all six markers
  absent; package 0.3.4 and provider/libmachook hashes unchanged.
- No new trustcache additions in R030. The R028 trustcache entries remain
  as recorded (`dd8a01ce…`, `2f0d5143…`; R024 reused
  `b3e29eef…`, `d0b584a8…`, `44767b6a…`).
- No production deployment or package change.

Evidence: `analysis/r030-f11h/` (retargeter, summary, retargeted record,
inspector, device error), device
`/var/mobile/macws_r030_evidence_20261018/`.
