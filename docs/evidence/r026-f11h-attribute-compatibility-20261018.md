# R026 — F11H serialized attribute-kind compatibility (offline)

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-026-to-backend.md`. Offline analysis only: the
preserved R014 request/module, the preserved original/converted MPSImage
metallibs, converter source, and the installed LLVM 16 library on the
device read **without executing any compiler, service or F11H endpoint**.
No source, package, Git, marker or production change. No attribute was
removed and no reader check was patched. Local scripts live under
`analysis/r026-f11h/`.

## 1. Result in one line

The rejected wire value is LLVM 16's **`ZeroExt`** enum (`AttrKind = 71`),
not `memory`; the type-15 module is **byte-identical** to the converter's
LLVM 16 `llvm-as` output, so the converter's re-serialization is the
producer of the rejected byte sequence. The existing converter can take a
different `--llvm-as`; re-encoding the same AIR with an `llvm-as` whose
enum ordering matches the reader is the only currently supported
semantics-preserving direction. The reader version remains unknown.

## 2. Inputs and exact identities

| Item | Value |
|---|---|
| R014/R024 type-15 data | 7,632 bytes, SHA-256 `ffa7daa51d8de4493c06c8c3e65717b116521d4e9fb6cc89fa8e89f91cbe2e90` |
| R024 diagnostic | `Unknown attribute kind (71) (Producer: 'LLVM16.0.0' Reader: 'LLVM ')` |
| type-15 AIR bitcode | 7,372 bytes, raw SHA-256 `042303be36e32a7c2755ffae074fc66e99bc6260009b3abcb970a5d87167b849` |
| converted F11H bitcode | 7,372 bytes, raw SHA-256 **identical** to the type-15 module |
| original F11H bitcode | 6,916 bytes, raw SHA-256 `ce018722f1d9058534380e6fa0aade64cdaef3cbbd0e806d02d769810273ec9b` |
| original MPSImage library | 16,448,932 bytes, SHA-256 `376ded7ee154429f6950656eb668b26af27fc6149b734b11dd48a33d68fe4285` |
| converted MPSImage library | 17,294,324 bytes, SHA-256 `342738608c912eab663879868288299e38147ea64d45b74f57bc0dd12761b2de` |
| converter | `misc/repack_metallib_macabi.py` SHA-256 `42f7909f870f532ce16e9e1eff326eb01bc00e0d3042d5a4bef4a00bb3389b5f` |
| profile | `misc/metal2metal_profiles.py` SHA-256 `1a538e2062e5d3faa602088113ef931a2f8113e2be4c718ffda741a1f477eac6` |
| CLI | `misc/metal2metal.py` SHA-256 `23742a879cb9a7d74e6b5cb1eb7e75742e6654ed011ed28e9848a018a0b92f17` |
| LLVM 16 lib | `/var/jb/usr/lib/llvm-16/lib/libLLVM.dylib`, SHA-256 `32589bf306ed80c08aa065afe084a87f3be4252947c835d22385129a09d9e3a6` |
| llvm-dis / llvm-as | `9e281a54902e3fba5aa78271ef486e8fb76fcfede1e08c58a561f04c82c452a6` / `36554b64bf63bca9d92400d8229f0e5b057a658b3b5b25c4321725b4cbbda236` |
| LLVM 16 version | `Procursus LLVM version 16.0.0`, default target `arm64-apple-ios16.0` |
| host decoder | Apple clang 17.0.0 (LLVM 17) |

The raw-bitcode equality is the decisive provenance fact: the client sends
the converter's F11H bitcode unchanged; it does not re-serialize the AIR.
The original Apple bitcode differs from the converted output in size and
bytes (6,916 vs 7,372), so the converter's `llvm-dis`/`llvm-as` round-trip
is the step that produced the exact rejected bytes.

## 3. Exact LLVM 16 mapping of kind 71

`_ZN4llvm9Attribute19getNameFromAttrKindENS0_8AttrKindE` in LLVM 16.0.0 is
a direct two-table lookup indexed by the enum value
(`sbfiz x8, x0, #3`), with the length table at `0x3cc77a8` and the data
pointer table at `0x5da6320`:

```
kind 69  length 10  pointer 0x3b4a903 -> "willreturn"
kind 70  length  9  pointer 0x3b4a90e -> "writeonly"
kind 71  length  7  pointer 0x3b4a918 -> "zeroext"
kind 72  length  5  pointer 0x3b4a920 -> "byref"
kind 73  length  5  pointer 0x3b4a926 -> "byval"
```

The exact bytes at `0x3b4a918` are `7a 65 72 6f 65 78 74 00`
(`zeroext\0`). This is the producer-side enum/name table of LLVM 16.0.0,
the version stated by the R024 `Producer` field. The serialized enum kind
71 is therefore LLVM 16 `ZeroExt`.

**Correction of the host-LLVM17 reading.** Apple clang 17 renders the
same module with `memory(argmem: readwrite)`, `memory(argmem: read)`,
`memory(none)` groups. That is a cross-version decode: LLVM 17's enum 71
is not LLVM 16's enum 71, so the textual `memory(...)` tokens are not
evidence that the rejected kind is `memory`. The LLVM 16 name table above
is authoritative; the host rendering cannot distinguish a real `memory`
attribute at another enum value from the kind-71 misdecode, so the
`memory(...)` tokens are not used as kind-71 evidence. This is exactly the ordinal-vs-
encoding trap the packet warned about.

## 4. Where the rejected bytes come from

1. The original MPSImage F11H module has the same textual attribute-group
   structure as the converted module and the type-15 module
   (`attributes.json`; the semantic attribute set was preserved across the
   conversion).
2. `retarget_air` runs `llvm-dis`, rewrites the single target triple,
   applies only the explicitly requested lowerings (`fract-v3f16`,
   `zero-memset-24`, half-float saturation), then runs `llvm-as`
   (`misc/repack_metallib_macabi.py:709-758`). It has no zeroext/attribute
   removal or renumbering step.
3. The converter's `llvm-as` is Procursus LLVM 16.0.0
   (`/var/jb/usr/lib/llvm-16/bin/llvm-as` in the profile scripts), and its
   output F11H bitcode is byte-identical to the type-15 module. The client
   forwards that bitcode; the `Producer: 'LLVM16.0.0'` in the R024
   diagnostic is consistent with the converter's writer.
4. The older reader rejects wire kind 71 before the module is built. The
   reader's version string is empty (`Reader: 'LLVM '`), so its exact LLVM
   version is **unknown** and is not invented here.

FACT: the converter preserves the semantic attribute set but re-serializes
it with LLVM 16 enum ordinals; the rejected byte sequence is the
converter's output (SHA equality).
THEORY (not yet proven): the original Apple module also carried `zeroext`
semantically; the converter did not introduce the attribute, only the
LLVM 16 wire ordinal. The original/converted group structures support
this, but the original's exact zeroext sites are not dumped.

## 5. Exact scope/occurrences — missing artifact

The version-skewed host LLVM 17 IR cannot enumerate the zeroext records:
it decodes kind 71 as a different attribute. The exact function/parameter
scope and all occurrences require an **LLVM 16 textual dump of the
preserved converted module** (the same bytes as the type-15 module).
That dump is a device execution (`/var/jb/usr/lib/llvm-16/bin/llvm-dis`),
which this packet explicitly defers. The precise next command is:

```
/var/jb/usr/lib/llvm-16/bin/llvm-dis <converted-F11H.bc> -o F11H.ll
```

and then list every `zeroext` on `declare`/`call` parameters and return
values. Candidate sites by AIR ABI are the integer AIR conversions
(`air.convert.f.f32.u.i16`, `air.convert.u.v2i32.u.v2i16`) and integer
parameters of the texture intrinsics; this is a candidate list, not an
accepted scope.

## 6. Semantics-preserving compatibility options

1. **Reader-matched re-encoding (preferred candidate).** The converter
   already accepts `--llvm-dis`/`--llvm-as` paths. If the reader is
   identified and an `llvm-as` of that LLVM version can parse the AIR text,
   re-assemble the same IR with it; enum ordinals (including `zeroext`)
   become reader-compatible without removing any attribute. This is a
   wire re-encoding, not a semantic change. It requires (a) the reader
   version, (b) confirmation that the older parser accepts the IR's other
   features (e.g., `memory(...)`), and (c) a private-endpoint A/B.
2. **Attribute-aware lowering.** If a reader-matched writer is
   unavailable, lower only the exact zeroext sites to explicit
   zero-extension (`zext`/mask) in the AIR text and drop the attribute,
   using the existing `apply_air_lowerings` framework
   (`repack_metallib_macabi.py:542-576`). This must preserve unsigned
   semantics; it is not blind stripping and needs the exact sites from §5.
3. **Reader-side update or client serialization patch.** Out of scope /
   not a converter fix; do not patch the reader check or weaken
   validation.

No option is implemented or accepted in this packet.

## 7. Next isolated A/B design (no device run now)

- **A0 (read-only, next packet):** LLVM 16 `llvm-dis` of the preserved
  converted module; record every zeroext declaration/call site and the
  real `memory(...)` attrs; record the reader version if it can be
  identified from the service image without a compiler run.
- **A1 (offline):** in a disposable converter copy, re-assemble the same
  AIR text with a reader-matched `llvm-as` (if available) and verify the
  raw zeroext kind equals the reader's ordinal; keep the triple/profile
  and all other attributes identical; compare raw bitcode hashes and
  textual IR semantics.
- **A2 (private endpoint, future):** one F11H control with the re-encoded
  converted module on the packet-owned endpoint; acceptance requires the
  service no longer aborting with `Unknown attribute kind (71)` and a real
  compiler reply or the next concrete error. No production endpoint,
  package or client change in this packet.
- Preserve the original/converted/type-15 hashes and the converter source
  hash in the A/B record.

## 8. Limits

- No LLVM 16 headers/`Attributes.inc`/`BitcodeWriter` source are installed
  on the device or host; the enum name table is extracted from the exact
  LLVM 16 binary, but the writer's enum-value encoding is inferred from
  the reader diagnostic and LLVM's enum-attribute wire contract.
- The host LLVM 17 textual IR is a cross-version decode and is not used to
  name kind 71.
- The exact zeroext scope/occurrences and the reader version remain
  missing (see §5); a device LLVM16 dump was deliberately not run.
- No device compiler/service/F11H run occurred; only read-only library,
  symbol and table reads plus local offline parsing.

## 9. Conclusion

The rejected serialized attribute is LLVM 16 `ZeroExt` (kind 71). The
converter's LLVM 16 `llvm-as` output is byte-identical to the type-15
module, so the converter's re-serialization is the producer of the
rejected wire bytes; the semantic attribute set was preserved, not
invented, and no attribute was stripped. A reader-matched writer (the
converter's existing `--llvm-as` parameter) is the supported
semantics-preserving direction, pending the exact zeroext sites and the
reader's LLVM version. The next artifact is the LLVM 16 textual dump of
the preserved module, followed by the isolated A/B above.

Evidence: `coordination/artifacts/R026-f11h-attribute-compatibility-20261018.json`,
`analysis/r026-f11h/` (extraction script, three modules and IR, LLVM16
table extraction, hashes).

## R028 correction — original slice definitions

R028 re-extracted F11H from the original metallib with full slice
accounting: the 6,916/`ce018722…` value labeled "original raw" here is the
**wrapper-declared payload**; the 6,924/`454ad619…` value is
payload plus the record's **8 zero padding** bytes; the full MTLB record is
6,944/`0fa8f789…` and the wrapper 6,936/`e3876cba…`. There is one
original record, not two. The kind-71/ZeroExt attribution remains
withdrawn; the frontend wire audit proves wire ID71 = `nocallback`. See
`r028-f11h-compatibility-candidate-20261018.md`.
