# R029 — independent saved R028 F11H candidate audit

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-029-to-frontend.md`. Read-only audit of the saved
backend artifacts. No shared/production/converter source change, build,
device action or Git write. Disposable scripts are under
`analysis/r029-candidate-audit/` (gitignored). Backend R028 was still Working
at snapshot time; this audit applies to the exact hashes below, not to a
future final candidate.

## 1. Stable snapshot

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| `candidate-F11H.record.bc` | 6,944 | `2f2de66bf924ca5e5e563a1c53822d967354ee323cbd53eac4e40dfc3a025b1b` |
| `mpsimage.r028-candidate.metallib` | 17,293,876 | `ebfd4f923b2dbd4cd684a5b4cb243b14c5cce5f573194d5de6d8dd2d2658fb37` |
| `type15.candidate.data` | 7,184 | `38f086cf8628975a9c32ae2244519e29380006ae1935740b849a916b51ecaf18` |
| `type15.control.data` | 7,632 | `ffa7daa51d8de4493c06c8c3e65717b116521d4e9fb6cc89fa8e89f91cbe2e90` |
| `build_candidate.py` | 5,170 | `fc3009f31d528c8d64b1a75be4224833369238156075fac49448fd424f2e65bc` |

The stable mtimes are 21:29 for the module/metallib and 21:33 for the request
pair; replay logs are 21:34. Backend final result/cleanup is pending.

## 2. `6916 vs 6924` and the candidate payload

The original wrapper declares `bcsize = 6916`. The F11H record is 6,944 bytes:

```text
20-byte wrapper + 6,916-byte declared payload + 8 zero trailing bytes
payload SHA-256 ce018722…ec9b
file-minus-header (6,924 B) SHA-256 454ad619…6264
```

The 6,916/`ce018722…` and 6,924/`454ad619…` values are therefore different
scopes, not a contradiction: the first is the declared bitcode payload; the
second adds the 8 trailing zero bytes that are part of the MTLB function
record but outside the declared payload. R026/R027 used different scopes.

The candidate preserves this exactly:

- `candidate-F11H.record.bc` is 6,944 B, wrapper `bcsize = 6916`, same 8
  trailing zero bytes;
- candidate payload (6,916 B) SHA-256
  `327f283d9cde50201639bd64ba3b7d4fbd35bbae54ddb47333e47b9e6bc20794`;
- full module SHA-256 `2f2de66b…5b1b`;
- the original and candidate modules have exactly 12 differing bytes, all
  inside the 24-byte AIR triple at offset 6,873-6,896; every prefix/suffix and
  trailing byte is identical.

The candidate tooling does **not** re-encode the bitstream. It patches a
same-length triple in place, so abbreviations, attribute-group references,
block sizes, body, constants, metadata and calling conventions in the F11H
module remain byte-identical to the original. The independent PARAMATTR_GROUP
parse returns the same 14 records as the original; no wire code 71 is present.

## 3. Container/request structural audit

The independent parser (`audit_candidate.py`, exit 0) checked the candidate
against the converted MTLB:

- 1,825 function records; 1,824 non-F11H modules are byte-identical to the
  converted modules; the F11H module is the candidate above.
- Every record's declared size equals `MDSZ`, every declared hash matches the
  actual module SHA-256, and every relative offset is sequential with no gaps
  or overlaps; the sum equals the bitcode section size.
- Candidate/converted header differences are only the file size and bitcode
  section size, both −448; function-list and public-metadata offsets/sizes are
  unchanged. Header-extension offset tags at/after the old bitcode end are
  shifted by −448; others are unchanged.
- Function-record tag groups are unchanged in size; only OFFT values after
  F11H shift, and only F11H's MDSZ/HASH change. No abbreviation/group/block
  size field is corrupted.

The request pair is also consistent:

- control: module size 7,392, total 7,632, wrapper `bcsize 7372`;
- candidate: module size 6,944, total 7,184, wrapper `bcsize 6916`;
- candidate header differs from control only in the six size fields
  (`0x64/0x68/0x74/0x84/0xa4/0xd0`); every other header byte is identical;
- the module at offset 260 is exactly `candidate-F11H.record.bc[20:]`
  (6,916 payload + 8 trailing zeros).

Host `clang` decoded the candidate bitcode successfully (exit 0,
`candidate.ll` SHA-256 `37b254f6…503d`); candidate/original and
candidate/converted decoded text differ only in the ModuleID line under the
host target override. This is a decode check only, not compiler acceptance.

## 4. Semantic fail: wrong target triple

The candidate chooses:

```text
candidate target triple:  air64-apple-ios19-macabi        (24 bytes)
converted module target:  air64-apple-ios19.0.0-macabi    (28 bytes)
source original target:   air64-apple-macosx13.4.0        (24 bytes)
```

`build_candidate.py` requires only that the replacement be the **same length**
as the source triple. It does not compare the target to the converted module
or a validated target profile. As a result the candidate is not a retarget to
the intended triple: `air64-apple-ios19-macabi` omits the `.0.0` component
present in the converted target.

The saved provisional replay is consistent with this fail:

```text
candidate reply: "errorMessage" => "Target OS is incompatible."
                 "error" => 2
control reply:   "XPCErrorDescription" => "Connection interrupted"
```

The candidate no longer hits `Unknown attribute kind (71)`, so it passes the
attribute-wire reader issue into a later target-OS check, but it is **not**
accepted and no compiler success is claimed. The exact service check is not
proven here; the objective candidate/target mismatch is sufficient to reject
the current artifact, and backend final no-hook evidence is still pending.

## 5. Construction semantics

The candidate is **not** a surgical restoration of only the intrinsic
declaration group inside the converted F11H module. It replaces the entire
F11H module with the original module plus a same-length triple patch, while
using converted modules everywhere else. For this snapshot the original and
converted F11H textual IR/body are identical apart from the ModuleID and raw
attribute encoding, so the observable body/attribute invariants happen to
hold; but the construction would discard any future non-attribute converter
change to F11H. A productized candidate must either prove this equivalence
for every field or perform targeted original-group restoration.

No reader/check patch is present in the candidate data or builder. The replay
service logs show the existing R024 diagnostic fatal-handler hook installed;
it is a diagnostic observer, not a reader/check patch, and the current replay
is provisional. Final acceptance must use the final backend artifacts without
relying on that hook.

## 6. Accept/reject and productization gates

**Accept (structural only):**

- bitstream module structure, abbreviations, group references, block sizes
  and trailing padding;
- original F11H attribute records/body/metadata/calling conventions;
- MTLB record offsets/hashes/sizes and request size fields.

**Reject (current candidate):**

- as a working compiler submission: the target triple is not the converted
  target and the provisional replay returns `Target OS is incompatible`;
- same-length arbitrary triple selection as a productization strategy: the
  correct converted triple is 28 bytes, so an in-place same-length patch
  cannot represent it.

**Future fail-closed gates:**

1. Candidate target triple must equal a validated converter/profile target,
   not merely be the same length as the source.
2. If the correct target triple length differs, use a bitstream-aware
   re-encode that restores the original intrinsic declaration attribute
   records; do not choose an arbitrary shorter/longer string.
3. Fail closed on any non-allowlisted attribute, body, metadata, constant,
   calling-convention, offset, hash, tag or size change.
4. Require final no-diagnostic-hook reader acceptance and a real compiler
   reply; the current provisional replay is not acceptance.
5. Keep the request container module/total sizes and wrapper `bcsize`
   consistent (verified for the saved candidate).
6. Add triple-length/equality regression tests and a fixture where a
   same-length but non-profile triple is rejected.

## 7. Pending input / limits

- Backend R028 is still Working; final candidate choice, final no-hook replay
  result/exit and cleanup are pending. This audit does not poll or message it.
- The candidate/control replay is a provisional saved log; the exact service
  target-OS check and the correct target/profile remain backend work.
- No shared/production source, converter, device, package, build or Git was
  changed. Only read-only audit scripts and evidence were written under
  `analysis/r029-candidate-audit/` and `docs/evidence/`.

Raw evidence: `analysis/r029-candidate-audit/audit_candidate.py`
(SHA-256 `178264a0…4ff3`), `audit-output.json`
(SHA-256 `31d912a3…9b1`), `candidate-audit.json`
(SHA-256 `eafb8982…54ab`), `replay-excerpts.txt`
(SHA-256 `50bd79ba…6663`), `snapshot-hashes.txt`
(SHA-256 `a43150ec…73ca`).

(handover to codex now)
