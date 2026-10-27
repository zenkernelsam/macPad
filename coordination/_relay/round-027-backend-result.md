# Backend R027 result — version-matched F11H LLVM16 decode

One authorized execution of the existing
`/var/jb/usr/lib/llvm-16/bin/llvm-dis` on disposable copies of the
preserved original and converted F11H modules. No AIR modification, no
attribute removal, no F11H control, no production compiler/WindowServer
run, no package/source/Git/trustcache/service change. Credential not
recorded.

## Withdrawal

**R026's `wire kind 71 = ZeroExt` attribution is withdrawn pending the
independent wand-mapping audit.** The LLVM16 textual dumps contain **no
`zeroext` and no `signext`** in either module. The internal
`Attribute::getNameFromAttrKind` table and the host LLVM17 `memory(...)`
render are not bitcode wire proof. The proposed zeroext lowering is not
implemented and must not be.

## Decode results

Tool: Procursus LLVM 16.0.0, `llvm-dis` SHA-256 `9e281a54…52a6`,
libLLVM `32589bf3…e3a6`.

| Input wrapper | Bytes | SHA-256 | Exit |
|---|---|---|---|
| original `orig-F11H.wrapped.bc` | 6,936 | `e3876cba…e6d5` | 0 |
| converted `conv-F11H.wrapped.bc` | 7,392 | `6aeb0047…28f7` | 0 |

Raw bitcode: original 6,916 bytes `ce018722…ec9b`; converted 7,372 bytes
`042303be…b849`, byte-identical to the R024 type-15 module. Both stderr
streams were empty.

Outputs: `orig-F11H.ll` 19,292 bytes `7a3105ba…9471`; `conv-F11H.ll`
19,296 bytes `88b791a7…b1f4`; both 283 lines.

## Exact differences and semantic preservation

The full diff is exactly two lines: the ModuleID and
`target triple` (`air64-apple-macosx13.4.0` → `air64-apple-ios19.0.0-macabi`).
After normalizing those, the dumps are identical
(`attribute-enumeration.json: normalized_equal_except_target_triple=true`).
The converter therefore preserved the textual IR, signatures, attributes
and body for F11H. This proves semantic preservation at the textual level
for this module; it says nothing about wire kind 71.

## Scoped attributes (identical in both dumps)

Groups: `#0` norecurse nounwind + strings; `#1` convergent nounwind +
strings; `#2` argmemonly nocallback nofree nosync nounwind willreturn;
`#3` argmemonly nounwind; `#4` argmemonly convergent nounwind readonly;
`#5` nounwind readnone; `#6` nounwind; `#7` convergent.

Functions: `_GLOBAL__sub_I…` #0; `F11H` #1; sample helper #1 align 2;
`llvm.lifetime.*` #2; `air.write_texture_2d` #3; `air.sample_texture_2d`
#4; `air.mix/fast_fract/fast_floor/dot/convert*` #5.

Parameters: `F11H %3` noalias nocapture readonly dereferenceable(128);
lifetime param 0 immarg / param 1 nocapture; write_texture_2d param 0
nocapture; sample_texture_2d params 0/1 nocapture readonly. **No
return-position attributes.** Call-site groups #3/#4/#5/#6/#7. Token
counts include argmemonly 7, readonly 15, readnone 7, willreturn 3;
**zeroext 0, signext 0.**

Producer strings: both `!llvm.ident` = Apple metal version 31001.722
(metalfe-31001.722.4); converted/type-15 bitcode producer LLVM16.0.0
(R024); original bitcode producer field not printed by the dump.

## Cleanup and state

The packet-owned temp directory was moved to
`/var/mobile/macws_r027_evidence_20261018/temp/` (wrapped inputs, `.ll`
dumps, empty stdout/stderr, `run.meta`, `tool-version.txt`); the temp path
is gone. Host copies are under `analysis/r027-f11h/device-evidence/`.
No `llvm-dis` process remains; production compiler count 14; native
SpringBoard 872 / tccd 883 / backboardd 905 unchanged; all six markers
absent; package 0.3.4 and provider/libmachook hashes unchanged.

## Next artifact

The frontend's independent LLVM16 wire-mapping audit. Do not implement the
zeroext lowering or any attribute removal until the wire kind is proven.

Evidence: `docs/evidence/r027-f11h-llvm16-decode-20261018.md`,
`analysis/r027-f11h/` (dumps, enumeration JSON, raw metadata), device
`/var/mobile/macws_r027_evidence_20261018/`.

(handover to codex now)
