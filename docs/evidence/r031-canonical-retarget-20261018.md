# R031 — canonical bitstream retarget (gates passed; replay not run)

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-031-to-backend.md`. Scope: one disposable
canonical re-encoder under `analysis/r031-f11h`, the exact 28-byte
`air64-apple-ios19.0.0-macabi` triple, host/device LLVM decode gates and
an independent record audit. A human worker-switch request
(`coordination/_relay/WORKER_SWITCH_HANDOVER_REQUEST.md`) arrived before
the bounded private-endpoint replay; per that request no replay was run
and no further experiment was started.

## 1. Result

- A complete LLVM-16 bitstream parser/writer was implemented from the
  release/16.x `BitstreamWriter.h`/`BitstreamReader.*` semantics
  (`analysis/r031-f11h/bcrewrite.py`,
  SHA-256 `9b34ab9742807e434c5a284a91c18e60ee4b81ef2222f4a4e2901395bd10e4e5`).
  It re-emits block scopes, code widths, abbreviation definitions
  (including BLOCKINFO-set inheritance), records, arrays, blobs, END
  markers, backpatched block lengths and the module VST/FNENTRY offsets.
  There are no redundant VBR chunks and no ad-hoc alignment splices.
- Gate 1 (unmodified-original roundtrip): re-emitting the parsed original
  payload reproduces it **byte-for-byte**:
  - original F11H payload 6,916 bytes /
    `ce018722f1d9058534380e6fa0aade64cdaef3cbbd0e806d02d769810273ec9b`;
  - converted F11H payload 7,372 bytes /
    `042303be36e32a7c2755ffae074fc66e99bc6260009b3abcb970a5d87167b849`.
  Both roundtrips are byte-identical including the STRTAB blob, SYMTAB,
  PARAMATTR/attribute-group blocks, metadata and all function bodies.
- Gate 2 (changed-triple candidate): only the un-abbreviated
  `MODULE_CODE_TRIPLE` (module block, field code 2) was replaced with the
  exact 28-byte `air64-apple-ios19.0.0-macabi`. The decoded-record audit
  reports **0 differences** in 21 blocks, 37 abbreviation definitions and
  476 records; the only allowed derived changes are `VSTOFFSET` and the
  three module-VST FNENTRY word offsets.
- Real-reader gates passed:
  - host LLVM 17 (`llvm-nm` and `clang -cc1 -emit-llvm-bc`, the latter
    fully materializing and re-emitting the module): exit 0 for both the
    original and candidate;
  - host text IR decodes are identical except `ModuleID`;
  - device LLVM-16 `llvm-dis` (the actual compiler reader): exit 0 for
    the original payload, the candidate payload and the 6,944-byte
    wrapped record; the only IR differences are `ModuleID` and
    `target triple` (verified below).
- No private compiler endpoint or production service was started, no
  A/B was run. The compiler question ("does the real MODULE_CODE_TRIPLE
  survive the target-OS check?") remains open until the bounded replay in
  §6.

## 2. Correction to R028/R029/R030

R028/R029/R030's in-place 24-byte string replacement changed the ASCII
`air64-apple-...` string inside the module **STRTAB blob** (payload byte
~6,853), not the `MODULE_CODE_TRIPLE` record at bits 12,800-13,103. The
module triple the compiler read was still
`air64-apple-macosx13.4.0`. Therefore:

- the R028 candidate's `Target OS is incompatible.` reply is **not**
  evidence about a correctly retargeted triple;
- any inference that a 28-byte/24-byte retarget reached the target-OS
  check is withdrawn;
- the R030 splice failure also says nothing about the target-OS path; it
  proved only that the splice was not a valid bitstream.

## 3. Exact wire change

Measured by the independent parser on the original and candidate payloads:

| Item | Original | Candidate | Delta |
|---|---|---|---|
| triple record | bits 12,800-13,102, 303 bits | bits 12,800-13,150, 351 bits | +48 bits |
| triple text | `air64-apple-macosx13.4.0` (24 chars) | `air64-apple-ios19.0.0-macabi` (28 chars) | +4 chars |
| module block words | 1,465 | 1,466 | +1 |
| module end | bit 47,136 | bit 47,168 | +32 bits |
| module VST start | bit 46,912 | bit 46,944 | +32 bits |
| `VSTOFFSET` value | 1,466 | 1,467 | +1 word |
| function blocks | 34,976 / 35,360 / 43,264 | 35,008 / 35,392 / 43,296 | +32 bits each |
| VST FNENTRY offsets | 1,093 / 1,105 / 1,352 | 1,094 / 1,106 / 1,353 | +1 word each |
| payload bytes | 6,916 | 6,920 | +4 |

The +48-bit record growth is partly absorbed by module/block padding, so
the module and VST move by one 32-bit word. Each VBR is canonical: code
3 (unabbreviated) at the 3-bit module code width, record code 2 and
element count 28 as one VBR6 chunk each, and every ASCII character as its
natural two-chunk VBR6 (5 data bits per chunk). Nothing else was
rewritten.

The writer uses two passes: pass 1 emits the original derived values to
learn the new absolute positions of the module VST and function blocks;
pass 2 rewrites the `VSTOFFSET` fixed-32 value and the VBR8 FNENTRY
offsets in the parsed records, then re-emits the whole stream. The
offsets are therefore re-encoded canonically, not patch-byte-overwritten.

Preserved from the original: 14 PARAMATTR groups (group 4
`[argmemonly,nounwind]`, **no ID71**), all attribute-group records, all
function bodies and metadata, the STRTAB/SYMTAB blocks, the BLOCKINFO
sets and every abbreviation definition. No `llvm-dis`/`llvm-as`
roundtrip and no attribute stripping was used for the candidate.

## 4. Gate evidence

Host LLVM 17 (`llvm-nm`, same symbol list for original and candidate;
exit 0/0) and full read+re-emit:

```
clang -cc1 -emit-llvm-bc -x ir <payload> -o <out> \
      -triple arm64-apple-macosx13.0 -Wno-override-module
original  -> exit 0, 7,440 bytes
candidate -> exit 0, 7,440 bytes
```

Both rewrites are byte-identical (`0dcaf243…172f`) because clang's
command-line triple override replaces the module triple in its own
output; the rewrite proves full bitstream materialization (all function
bodies were needed to re-serialize), not the wire triple.

Device LLVM-16 `llvm-dis` (disposable copies, exit 0/0/0):

```
orig.llvm16.ll:        target triple = "air64-apple-macosx13.4.0"
cand.llvm16.ll:        target triple = "air64-apple-ios19.0.0-macabi"
cand-record.llvm16.ll: target triple = "air64-apple-ios19.0.0-macabi"
diff: only "; ModuleID = ..." and the target triple line
```

Independent record/semantic audit (`audit_module` in `bcrewrite.py`):
21 blocks, 37 abbreviations, 476 records, 1 triple record, 1 VSTOFFSET
record, 3 FNENTRY records; differences: **0**.

## 5. Artifacts and hashes

| Artifact | Bytes | SHA-256 |
|---|---|---|
| `analysis/r031-f11h/checkpoint` (`CHECKPOINT.md`) | — | see file |
| `bcrewrite.py` | — | `9b34ab9742807e434c5a284a91c18e60ee4b81ef2222f4a4e2901395bd10e4e5` |
| `build_r031_artifacts.py` | — | `0bb1bb8b6f4880ea5256df446b63bce8e5a90f45e0c3bd4a7059ce7c7059e7b5` |
| `roundtrip-orig.payload.bc` | 6,916 | `ce018722f1d9058534380e6fa0aade64cdaef3cbbd0e806d02d769810273ec9b` |
| `roundtrip-conv.payload.bc` | 7,372 | `042303be36e32a7c2755ffae074fc66e99bc6260009b3abcb970a5d87167b849` |
| `retargeted-F11H.payload.bc` | 6,920 | `8dc40d786b9e82befaa9e2d7166d88dd63ad9d64339cb293616656acf749c269` |
| `r031-candidate.record` | 6,944 | `5d805ec5f65c94c94377370659ebd9f8eae71749486b272ba6c4ed68268252cb` |
| `r031-candidate.type15.data` | 7,184 | `75663758aa1a914e1cd538ec22690ee73d9e0f9df52ea6c43f56032f40d3da1e` |
| `mpsimage.r031-candidate.metallib` | 17,293,876 | `7b71a6479ed162bf4cf4af2445978d7bb849b529d502e9254d3ad67006803aae` |

The record is the 20-byte `0x0B17C0DE` wrapper (`bcoff=20`,
`bcsize=6,920`) plus the payload plus 4 zero bytes of 16-byte record
padding. The MTLB was rebuilt from
`analysis/r006-mps-f11h/mpsimage.conv.metallib`; the bitcode section is
448 bytes shorter, all 1,824 other function modules are byte-identical,
and only the F11H MDSZ/HASH/OFFT and container bitcode size/offsets
changed. The type-15 container reuses the exact R014/R028 0xF0-byte
prefix layout; only the module-size word at 0x64 and the total-size words
at 0x68/0x74/0x84/0xA4/0xD0 were updated (record 7,392→6,944, total
7,632→7,184).

LLVM 16 sources used as the writer specification (release/16.x):
`BitstreamWriter.h` `896b67097d8…`, `BitstreamReader.h`
`b844c03bfe8…`, `BitstreamReader.cpp` `8c8fa712f8c…`, `LLVMBitCodes.h`
`41c9d3803cc…`, `BitcodeReader.cpp` `171ba64ae6c…`, `BitcodeWriter.cpp`
`6da61902bc4…` under `analysis/r031-f11h/llvm16src/`.

## 6. Not run — exact remaining work

The bounded private control/candidate replay was **not** run (worker
switch). It is the only remaining compiler step:

1. Restore the R028-owned endpoint from
   `/var/mobile/macws_r028_evidence_20261018/owned/r014/` to the
   redirect's hardcoded live path
   `/var/jb/usr/macOS/Frameworks/Metal.framework/XPCServices/MacWS/r014/`
   (`MTLCompilerService.xpc`, `service/`, `client/`). The bundle
   executable is `r024_wrapper` (CDHash `44767b6a…`), loading
   `MTLCompilerService.real` (`b3e29eef…`) and `r024_diag.dylib`
   (`d0b584a8…`); all three trustcache entries are live.
2. Control: `client/r028/type15.control.data`, 7,632 bytes /
   `ffa7daa51d8de4493c06c8c3e65717b116521d4e9fb6cc89fa8e89f91cbe2e90`
   (converted module; expected `Unknown attribute kind (71)` fatal).
3. Candidate: the new `r031-candidate.type15.data`, 7,184 bytes /
   `75663758aa1a914e1cd538ec22690ee73d9e0f9df52ea6c43f56032f40d3da1e`.
   Run `client/r028_replay_client` (CDHash `2f0d5143…`, trustcache live)
   with `DYLD_INSERT_LIBRARIES=client/r014_redirect_v6.dylib` (CDHash
   `1bb93be6…`) inside the chroot, exactly as in R028 §5.2.
4. A real reply is not success: only a fully specialized function (or the
   same no-error reply with a real object/metallib) counts. If the
   candidate succeeds, re-run it with a **no-R024-hook** endpoint
   (`r014_service_wrapper4` or `r014_service_wrapper` with the unpatched
   `superseded/MTLCompilerService.real.orig`, SHA-256 `4d318283…`) before
   claiming success.
5. Cleanup after the replay: move the live `MacWS/r014` tree back to the
   evidence directory, leave no owned process, re-check markers and the
   production compiler count.

## 7. Device state and cleanup (this round)

- Device used: `192.168.68.63:2222` (`Cs-Pad-2`, iPad13,11). The most
  recent `192.168.68.62:2222` endpoint was unreachable (connection
  refused; port 22 password auth denied); `.63:2222` was authorized in
  earlier rounds of this conversation and holds the R028/R030 evidence.
  The credential was transient and is not recorded anywhere.
- Device modification: only the new
  `/var/mobile/macws_r031_evidence_20261018/temp/` directory containing
  the four staged inputs plus three device `llvm-dis` outputs (7 files).
  No endpoint was restored, no service was launched, no trustcache entry
  was added, no production/source/package/marker change.
- Owned processes: none. No `llvm-dis`/redirect/replay process remains;
  the live `MacWS/r014` path is absent; all six diagnostic markers are
  absent.
- Baseline re-check: native SpringBoard 872 / tccd 883 / backboardd 905
  unchanged; package `0.3.4`; provider `b2ae21da…`, libmachook arm64e
  `47f78880…`, arm64 `6de9b688…` unchanged; production compiler count 15
  before and after (R028 recorded 14, so one stock on-demand instance was
  already present before this round).
- Observed, not caused by this round: system-side crash reports at
  22:28-22:33 (`macwsdisplayd`, `macwsinputd`, `macwsinteropd`,
  `UIKitSystem`, `CoreLocationAgent`) and a **production-path**
  `MTLCompilerService` crash at 22:31:29. No private endpoint existed, so
  these are not attributable to a replay. Per the switch request, the
  next worker should first recover the known visible wallpaper/Dock/menu
  bar state before any production integration.

## 8. Risks / rejected hypotheses

- Rejected: byte/splice retargeting, redundant VBR chunks (R030),
  llvm-dis/as roundtrip (reintroduces the LLVM-16 intrinsic attributes),
  and attribute stripping. None was used.
- Host clang always replaces the module triple in its own emitted output
  (with `-Woverride-module`); it cannot demonstrate the candidate's wire
  triple. The wire triple is demonstrated by the independent parser and
  the device LLVM-16 `llvm-dis`.
- The candidate may still be rejected by the compiler's target-OS check;
  this is unknown until the bounded replay. The 28-byte triple is the
  runtime-confirmed profile string, but no reply has yet been observed
  for a module that actually carries it.
- The device currently shows independent system crash activity; verify
  the host stack is healthy before the replay.

Evidence tree: `analysis/r031-f11h/`, device
`/var/mobile/macws_r031_evidence_20261018/`.

(handover to codex now)
