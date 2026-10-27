# R028 — F11H provenance reconciliation and isolated compatibility candidate

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-028-to-backend.md`. One bounded direct type-15
control vs candidate through the packet-owned private compiler endpoint,
after the provenance/equivalence checks. No production
compiler/WS/generation/package/source/Git/policy/marker change. The
credential was transient and is not recorded.

## 1. Result

- The two disputed “original” artifacts are **slice definitions of the
  same MTLB record**, not different originals (§2).
- The only semantic wire change between original and converted F11H is the
  **LLVM16 intrinsic-attribute auto-upgrade on the lifetime declarations**
  (original `[argmemonly,nounwind]` → converted
  `[argmemonly,nocallback,nofree,nosync,nounwind,willreturn]`). R027's
  textual equality was the reader's in-memory upgrade, not wire
  preservation (§3).
- A candidate was built by **retargeting the original F11H record in place**
  from the 24-byte `air64-apple-macosx13.4.0` to the same-length
  `air64-apple-ios19-macabi`, preserving every original wire attribute,
  the body and metadata; no llvm-dis/llvm-as round-trip and no attribute
  removal (§4).
- Direct type-15 A/B: control (converted) → `Unknown attribute kind (71)`
  fatal / `Connection interrupted`; candidate → **no ID71**, real compiler
  reply `errorMessage = "Target OS is incompatible."`, `error = 2`.
  The candidate bypassed the unsupported wire attribute; the next blocker
  is the target-OS check (§5).
- The library-path A/B attempt was invalid for candidate testing: the
  metal2metal adapter routed to the installed companion, so both runs sent
  the converted module. That is recorded as a harness limit, not as
  candidate acceptance (§5.1).

## 2. Provenance reconciliation

`F11H` in `analysis/r006-mps-f11h/mpsimage.orig.metallib`:

| Slice | Bytes | SHA-256 |
|---|---|---|
| MTLB function record (`bitcode_offset+OFFT`, `MDSZ`) | 6,944 | `0fa8f789101d5c7d9f292e0ccc068950d7495a2a853588b4067ccdd873778e2c` |
| wrapper (`bcoff=20`, `bcsize=6,916`) | 6,936 | `e3876cbacf3936f328ea1e025e917aa51bf712e7bfb8a264ec759ed5b888e6d5` |
| wrapper-declared bitcode payload | 6,916 | `ce018722f1d9058534380e6fa0aade64cdaef3cbbd0e806d02d769810273ec9b` |
| record minus wrapper header (payload + 8 zero bytes) | 6,924 | `454ad619475cc081e7d57de78d6d4e2f0abdc818c5c7d38e230f72739c806264` |

The 8 trailing bytes in the record are `00 00 00 00 00 00 00 00`
(container padding). The frontend's 6,924/`454ad619` is the
record-minus-header slice **including that padding**; the backend's
6,916/`ce018722` is the wrapper-declared payload. Both come from the same
record at 11,426,375; there is no conflicting original. The bitcode reader
uses the wrapper's `bcsize`; the padding is ignored.

Converted F11H: record 7,392, wrapper 7,392/`6aeb0047…`, payload
7,372/`042303be…`, no trailing padding. The type-15 request raw module is
byte-identical to the converted payload.

## 3. Exact wire groups and the auto-upgrade

Independent wire parser (`analysis/r027-attribute-wire/parse_paramattr.py`)
on the preserved modules:

```
original (14 PARAMATTR groups):
  grp=4 idx=FUNCTION raw=[0,45, 0,18]          -> argmemonly, nounwind
  grp=7 idx=FUNCTION raw=[0,45, 0,43, 0,18, 0,21]
                                                -> argmemonly, convergent, nounwind, readonly

converted / request (16 groups):
  grp=4 idx=FUNCTION raw=[0,45, 0,71, 0,62, 0,63, 0,18, 0,61]
      -> argmemonly, nocallback(71), nofree(62), nosync(63), nounwind, willreturn
  grp=7 idx=FUNCTION raw=[0,45, 0,18]           -> argmemonly, nounwind
  grp=9 idx=FUNCTION raw=[0,45, 0,43, 0,18, 0,21]
```

The converted/request has one extra group because the original lifetime and
write-texture declarations shared `[argmemonly,nounwind]`; after the
lifetime upgrade they needed separate groups (group split/renumbering is a
serialization consequence). The only semantic change is the lifetime
group's four added attributes.

Exact LLVM16 upgrade source (public tag `llvmorg-16.0.0`, fetched analysis
copies under `analysis/r028-f11h/llvm16/`):

- `BitcodeReader.cpp:3676-3684` (`globalCleanup`) calls
  `UpgradeIntrinsicFunction(&F, NewFn)` for every function and then
  `UpgradeFunctionAttributes(F)`.
- `AutoUpgrade.cpp:1174-1181` (`UpgradeIntrinsicFunction`) calls
  `F->setAttributes(Intrinsic::getAttributes(F->getContext(), id))` for
  every intrinsic.
- `Function.cpp:418-422` (`Function::Function`) sets
  `Intrinsic::getAttributes(...)` when the function name is a recognized
  intrinsic.
- `Intrinsics.td:1191-1196` defines `int_lifetime_start/end` as
  `DefaultAttrsIntrinsic` with `IntrArgMemOnly, IntrWillReturn,
  NoCapture<ArgIndex<1>>, ImmArg<ArgIndex<0>>`; the default intrinsic
  properties supply `nocallback/nofree/nosync` and the wire also carries
  `willreturn`.

`llvm-dis` therefore shows the upgraded lifetime group for **both** the
original and converted modules even though the original wire lacks those
attributes. R027's normalized textual equality does not prove original
wire preservation. The frontend's independent wire audit remains
authoritative: wire ID71 = `nocallback`, not ZeroExt; the R026 zeroext
attribution stays withdrawn.

## 4. Candidate construction

- Original F11H record (6,944) retargeted with the converter's validated
  same-length `retarget_air_in_place`: `air64-apple-macosx13.4.0` (24) →
  `air64-apple-ios19-macabi` (24), exactly one triple match, same length.
- Candidate module: 6,944 bytes, SHA-256
  `2f2de66bf924ca5e5e563a1c53822d967354ee323cbd53eac4e40dfc3a025b1b`;
  differs from the original only in 12 suffix bytes inside the triple
  (shared `air64-apple-` prefix unchanged). Wire groups: 14, identical to
  the original (no ID71). Body, metadata and all other attributes are
  byte-preserved.
- Candidate metallib built from a copy of the converted container: F11H
  record replaced; bitcode section 14,739,456 → 14,739,008 (−448); all
  1,824 other function modules byte-identical to converted; MTLB
  platform/target/version (0x8001/macabi/2.7/19.0) unchanged; record
  hashes/offsets/trailing-section offsets rebuilt and verified. Candidate
  metallib: 17,293,876 bytes, SHA-256
  `ebfd4f923b2dbd4cd684a5b4cb243b14c5cce5f573194d5de6d8dd2d2658fb37`.
- Candidate type-15 data for the direct replay: 7,184 bytes, SHA-256
  `38f086cf8628975a9c32ae2244519e29380006ae1935740b849a916b51ecaf18`;
  archive fields updated (module record size 0x64 = 0x1B20, total fields
  0x68/0x74/0x84/0xA4/0xD0 = 0x1C10, wrapper `bcsize` = 0x1B04).

Builder/verifier: `analysis/r028-f11h/build_candidate.py` (SHA-256
`fc3009f31d528c8d64b1a75be4224833369238156075fac49448fd424f2e65bc`),
`provenance.json`, `candidate-summary.json`.

## 5. A/B results

### 5.1 Library-path attempt (invalid for candidate testing)

The disposable client loaded the candidate metallib
(`STAGE=library RESULT=ok`), but the request data remained the installed
converted module (length 7,632, FNV `0b867f3663545473`, identical to the
control) because the metal2metal adapter's route directory
(`/usr/local/share/macws/metal2metal/routes`) resolves the installed
companion. Both runs hit ID71. This attempt is recorded as a harness
limit; it is not used as candidate evidence.

### 5.2 Direct type-15 replay (valid)

A minimal replay client sends the captured request dictionary
(`requestType=15`, `data`, `llvmVersion=31001`, `client_name`,
`APISpecifiedTimeoutInSeconds=0`) to the private endpoint through the
verified redirect; the R024 diagnostic hook is loaded at
`MTLCompiler+0x7985c` (prologue changed=1).

Control (`type15.control.data`, 7,632 bytes,
`ffa7daa51d8de4493c06c8c3e65717b116521d4e9fb6cc89fa8e89f91cbe2e90`):

```
R014-TYPE15 ... length=7632 fnv1a64=0b867f3663545473
R024-DIAG-FATAL ... PAIR @56 len=68:
  Unknown attribute kind (71) (Producer: 'LLVM16.0.0' Reader: 'LLVM ')
REPLY "XPCErrorDescription" = "Connection interrupted"
service pid 90530
```

Candidate (`type15.candidate.data`, 7,184 bytes,
`38f086cf8628975a9c32ae2244519e29380006ae1935740b849a916b51ecaf18`):

```
R014-TYPE15 ... length=7184 fnv1a64=8d7a319691fc99eb
R024-DIAG-LOADED pid=90568 ... HOOK-INSTALLED ... changed=1
(no R024-DIAG-FATAL)
REPLY { "errorMessage" = "Target OS is incompatible." (26 bytes),
        "error" = 2 (uint64) }
service pid 90568 (exited)
```

Interpretation: the candidate is accepted by the older reader (no ID71);
the compiler then rejects the target-OS context. This is a **real
compiler reply with a downstream error**, not a connection interruption
and not a wire-attribute abort. The disappearance of ID71 is necessary
but not sufficient; the candidate did not produce a fully specialized
function. The target triple `air64-apple-ios19-macabi` (24, normalized
19.0.0) is not accepted by this compiler's target check; the
runtime-confirmed profile target is the 28-byte `air64-apple-ios19.0.0-macabi`.
A real bitstream re-encoder for the 28-byte triple that preserves the
original attribute records is the next required artifact; a byte patch is
not acceptable because the length changes.

## 6. State and cleanup

- Owned endpoint/client/data moved to
  `/var/mobile/macws_r028_evidence_20261018/` (`owned/r014`,
  `owned/r028/mpsimage.r028-candidate.metallib`, `logs/diag-raw.log`,
  `crashes/`). The production `MacWS/r014` and `MacWS/r028` paths are
  absent.
- No owned process remains; production compiler count 14.
- Native SpringBoard 872 / tccd 883 / backboardd 905 unchanged; package
  0.3.4; provider and both libmachook hashes unchanged; all six markers
  absent.
- Trustcache additions: R024 endpoint reused (`b3e29eef…` patched real,
  `d0b584a8…` diagnostic, `44767b6a…` wrapper); R028 clients
  `dd8a01ce…` (`r028_f11h_client`) and `2f0d5143…`
  (`r028_replay_client`). No production helper was added.
- Thermal was not re-sampled in this packet; no fresh claim.

## 7. Corrections to prior docs

- R026/R027 slice definitions: the disputed 6,924/`454ad619` is the
  record-minus-wrapper-header (payload + 8 zero padding); the
  6,916/`ce018722` is the wrapper-declared payload. The full record is
  6,944/`0fa8f789`; the wrapper is 6,936/`e3876cba`. There is one original
  record, not two.
- R027's textual equality is explained by the LLVM16 intrinsic
  auto-upgrade on read; it does not prove the original wire attributes
  were preserved. The wire data (original lacks ID71; converted has it)
  stands.
- R026's `wire kind 71 = ZeroExt` remains withdrawn; wire ID71 =
  `nocallback`.

## 8. Limits / next artifacts

- The direct replay tests the exact type-15 data archive, not the full
  library/companion path; the candidate metallib was built and verified
  but the adapter routing prevented its library-path exercise.
- The candidate target string is a same-length 24-byte form; the compiler
  rejects it with `Target OS is incompatible.` The next artifact is a
  real bitstream re-encoder (or a supported writer) that changes the
  original record's triple to the 28-byte `air64-apple-ios19.0.0-macabi`
  while preserving all other records, followed by the same direct A/B.
- No full specialized function was produced; the target-OS check is the
  next concrete blocker, not an attribute-stripping problem.

Evidence: `analysis/r028-f11h/` (provenance/candidate JSON, builder,
modules, control/candidate data, direct replay logs, device evidence),
device `/var/mobile/macws_r028_evidence_20261018/`.
