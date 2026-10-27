# Qoder final backend handover — R031 (worker switch)

Date: 2026-10-18 (Asia/Hong_Kong). Written per
`coordination/_relay/WORKER_SWITCH_HANDOVER_REQUEST.md` at the human's
request to switch from Qoder back to Devin CLI. No new experiment was
started; no deploy/commit/rollback/device shutdown was performed.

## 1. Current task and exact status

R031 (`coordination/_relay/round-031-to-backend.md`) asked for a full
canonical bitstream writer/re-encoder, an unmodified-original roundtrip
gate, a real 28-byte `air64-apple-ios19.0.0-macabi` retarget, host/device
LLVM decode gates and an independent record audit, followed by at most
one bounded private control/candidate replay.

Status: **implementation and all gates passed; the replay was not run.**
The switch request arrived before the replay and explicitly forbids
starting new experiments, so the private compiler endpoint stayed
untouched and the candidate is unaccepted at the compiler level. Nothing
is known yet about whether the real `MODULE_CODE_TRIPLE` survives the
compiler's target-OS check.

Key documents:

- `docs/evidence/r031-canonical-retarget-20261018.md` — full evidence.
- `coordination/_relay/round-031-backend-result.md` — round result.
- `analysis/r031-f11h/CHECKPOINT.md` — self-contained restart note.
- `AGENTS.md`, `coordination/_relay/SYNC.md` and the R028/R030 result
  files remain the operating memory.
- Latest independent review: `docs/evidence/r032-r031-candidate-audit-
  20261018.md` re-hashed the six R031 artifacts and found them identical
  (`roundtrip-orig` `ce018722…`, `roundtrip-conv` `042303be…`, candidate
  payload `8dc40d78…`, record `5d805ec5…`, metallib `7b71a647…`, type-15
  `75663758…`); it is a local audit, not compiler acceptance.
  `docs/evidence/r031-validator-llvm-consistency-20261018.md` covers the
  older R030 artifacts and does not change this result.


## 2. Completed work

- `analysis/r031-f11h/bcrewrite.py`
  (`9b34ab9742807e434c5a284a91c18e60ee4b81ef2222f4a4e2901395bd10e4e5`)
  parses the complete bitstream (blocks, code widths, abbreviation
  definitions with BLOCKINFO inheritance, records, arrays, blobs,
  END markers, block lengths, module VST/FNENTRY offsets) and re-emits
  it with LLVM-16 `BitstreamWriter` semantics. No redundant VBR chunks,
  no splices, no reader patches.
- Roundtrips are byte-identical for both preserved payloads: original
  6,916/`ce018722…ec9b` and converted 7,372/`042303be…b849`.
- Candidate: exact 28-byte `air64-apple-ios19.0.0-macabi`. Triple record
  303→351 bits; module words 1,465→1,466; VST start 46,912→46,944;
  `VSTOFFSET` 1,466→1,467; function blocks +32 bits; FNENTRY
  1,093/1,105/1,352→1,094/1,106/1,353. Original attribute groups
  (14, group 4 `[argmemonly,nounwind]`, no ID71), bodies, metadata,
  constants, STRTAB/SYMTAB and abbreviation sets are preserved.
- Independent decoded-record audit: 21 blocks, 37 abbreviations, 476
  records, 0 differences outside the triple/VSTOFFSET/FNENTRY fields.
- Host LLVM 17: `llvm-nm` exit 0 both; `clang -cc1 -emit-llvm-bc` (full
  materialize+re-emit) exit 0 both; text IR identical modulo `ModuleID`.
- Device LLVM-16 `llvm-dis`: exit 0 for original payload, candidate
  payload and candidate wrapped record; the only IR diff is `ModuleID`
  plus `target triple` (`air64-apple-macosx13.4.0` →
  `air64-apple-ios19.0.0-macabi`).
- Candidate record (6,944), type-15 data (7,184) and rebuilt MTLB
  (17,293,876) are built; the type-15 prefix is the R014/R028 0xF0-byte
  layout with only module/total size words updated.

## 3. Pending work (do not treat as done)

One bounded private control/candidate replay under the R028/R030
isolation recipe, then a no-diagnostic-hook confirmation if it succeeds.
Exact steps and hashes are in
`docs/evidence/r031-canonical-retarget-20261018.md` §6. Summary:

1. Restore
   `/var/mobile/macws_r028_evidence_20261018/owned/r014/{MTLCompilerService.xpc,service,client}`
   to
   `/var/jb/usr/macOS/Frameworks/Metal.framework/XPCServices/MacWS/r014/`.
   Bundle executable `r024_wrapper` (`44767b6a…`), `.real`
   (`b3e29eef…`), diag `d0b584a8…`; trustcache entries live.
2. Control `client/r028/type15.control.data` (7,632/`ffa7daa5…e90`,
   expected ID71 fatal).
3. Candidate `r031-candidate.type15.data` (7,184/`75663758…da1e`) via
   `client/r028_replay_client` (`2f0d5143…`) and
   `DYLD_INSERT_LIBRARIES=client/r014_redirect_v6.dylib` (`1bb93be6…`)
   inside the chroot.
4. Real success requires a fully specialized function/real reply and a
   re-run with a no-R024-hook endpoint (`r014_service_wrapper4` /
   unpatched `superseded/MTLCompilerService.real.orig`, `4d318283…`).
5. Move the live endpoint tree back and re-check processes/markers.

## 4. Source, package and device modifications

- Source/package/Git: none. No commit, no package install, no
  production service, WindowServer or generation was touched.
- Host additions only: `analysis/r031-f11h/` (scripts, LLVM-16 reference
  sources, roundtrips, candidate payload/record/type15/metallib,
  host-gate outputs, device-evidence `.ll` copies), the R031 evidence
  doc, the R031 result doc and this handover. The candidate artifacts
  are disposable analysis products, not production inputs.
- Device additions only: `/var/mobile/macws_r031_evidence_20261018/temp/`
  with 7 files (4 staged inputs, 3 `llvm-dis` outputs, hashes in the
  evidence doc). No endpoint restored, no service launched, no
  trustcache addition, no marker created.
- Device endpoint change: the most recent `<historical-device>:<port>` was
  unreachable (connection refused; `<historical-device>:22` rejected password auth).
  Work used `$MACWS_DEVICE:$MACWS_DEVICE_PORT` (iPad13,11), authorized in
  earlier rounds of this conversation and holding the R028/R030
  evidence. No credential or address is recorded in any file.

## 5. Artifact paths and hashes

| Path | Bytes | SHA-256 |
|---|---|---|
| `analysis/r031-f11h/bcrewrite.py` | — | `9b34ab9742807e434c5a284a91c18e60ee4b81ef2222f4a4e2901395bd10e4e5` |
| `analysis/r031-f11h/build_r031_artifacts.py` | — | `0bb1bb8b6f4880ea5256df446b63bce8e5a90f45e0c3bd4a7059ce7c7059e7b5` |
| `analysis/r031-f11h/retargeted-F11H.payload.bc` | 6,920 | `8dc40d786b9e82befaa9e2d7166d88dd63ad9d64339cb293616656acf749c269` |
| `analysis/r031-f11h/r031-candidate.record` | 6,944 | `5d805ec5f65c94c94377370659ebd9f8eae71749486b272ba6c4ed68268252cb` |
| `analysis/r031-f11h/r031-candidate.type15.data` | 7,184 | `75663758aa1a914e1cd538ec22690ee73d9e0f9df52ea6c43f56032f40d3da1e` |
| `analysis/r031-f11h/mpsimage.r031-candidate.metallib` | 17,293,876 | `7b71a6479ed162bf4cf4af2445978d7bb849b529d502e9254d3ad67006803aae` |
| `analysis/r031-f11h/device-evidence/orig.llvm16.ll` | — | `4c2fa6e02a737ad7c272e685966e0dee0498903b8db008effb75c0dff11a7011` |
| `analysis/r031-f11h/device-evidence/cand.llvm16.ll` | — | `8ba8c206fbcdb587c5dcb9068cb278fbae7d683dd5bb71d13a128f1a258ae90d` |
| `analysis/r031-f11h/device-evidence/cand-record.llvm16.ll` | — | `4b4e72501fa1144f8045da5098821881a7665fe037a416e1f5638e5a2970c521` |

Original/control references: original record 6,944/`0fa8f789…`; original
payload 6,916/`ce018722…`; converted payload 7,372/`042303be…`; control
type-15 7,632/`ffa7daa5…`.

## 6. Owned processes and cleanup

- Host: no background process remains; the only background command
  started (a grep) was stopped. No dev server, compiler or SSH session is
  being held open.
- Device: no owned process (`llvm-dis` completed; no replay client or
  endpoint ever started). The live `MacWS/r014` path is absent, and all
  six diagnostic markers are absent.
- Baseline after cleanup: native SpringBoard 872 / tccd 883 /
  backboardd 905; package 0.3.4; provider `b2ae21da…`; libmachook
  arm64e `47f78880…` / arm64 `6de9b688…`; production compiler count 15
  before and after. R028 recorded 14; one stock on-demand instance was
  already present at the start of this round.
- Observed but not caused by this round: system-side crash reports at
  22:28-22:33 (`macwsdisplayd`, `macwsinputd`, `macwsinteropd`,
  `UIKitSystem`, `CoreLocationAgent`) and a production-path
  `MTLCompilerService` crash at 22:31:29. No private endpoint existed, so
  no replay can be implicated. **Before any further compiler work,
  recover the last known visible wallpaper / Dock / menu bar state** and
  confirm the host stack is healthy.

## 7. Candidate limits and rejected hypotheses

- The candidate has never been sent to a real compiler. Gates prove
  bitstream validity and semantic preservation only; they are not
  compiler acceptance and not pixel/UI success.
- Do not claim that a target-OS error was ever produced for a correctly
  retargeted module: R028's in-place string replacement touched the
  STRTAB blob, not MODULE_CODE_TRIPLE. That inference is withdrawn.
- Rejected: byte/splice retargeting, redundant VBR chunks, llvm-dis/as
  roundtrip (reintroduces LLVM-16 intrinsic attributes), attribute
  stripping, reader patching.
- Host clang normalizes the module triple in its own emitted output
  (`-Woverride-module`), so host text/bitcode rewrites cannot prove the
  wire triple; use the device LLVM-16 `llvm-dis` plus the parser audit.
- Remaining risk: the compiler may still reject the target-OS context;
  no-hook confirmation is mandatory before any success claim.

## 8. Reproducible next commands

Host gates (no secrets):

```bash
NM=$(xcrun --find llvm-nm)
$NM analysis/r031-f11h/retargeted-F11H.payload.bc          # exit 0
clang -cc1 -emit-llvm-bc -x ir \
  analysis/r031-f11h/retargeted-F11H.payload.bc -o /tmp/out.bc \
  -triple arm64-apple-macosx13.0 -Wno-override-module      # exit 0
python3 analysis/r031-f11h/bcrewrite.py --inspect \
  analysis/r031-f11h/retargeted-F11H.payload.bc
python3 analysis/r031-f11h/bcrewrite.py --retarget \
  analysis/r026-f11h/orig-F11H.bc /tmp/retarget.bc \
  air64-apple-ios19.0.0-macabi
```

Device (set `MACWS_DEVICE`/`MACWS_DEVICE_PORT` from the active session and
supply the password only transiently through the SSH helper; never write
it to a file). Device decode and replay paths are exactly those in the
evidence doc §4/§6; the R028 recipe (`docs/evidence/
r028-f11h-compatibility-candidate-20261018.md` §5.2) is still valid.

## 9. When it is safe to close this Qoder CLI

Yes — it is safe now. All Qoder-owned commands have finished, no
background task or SSH connection is held, the device has no owned
process or live endpoint, and the R031 handover artifacts are written.
No further device action will happen from this session. The device itself
must stay running (do not shut it down); the next worker should first
check the visible wallpaper/Dock/menu bar recovery state.

(handover to codex now)
