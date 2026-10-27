# Qoder frontend — final handover to Devin CLI

Date: 2026-10-18 (Asia/Hong_Kong). Worker: qoder frontend (read-only audit/
provider contract/handover role). No passwords or connection secrets are in
this file. The qoder CLI is safe to close after this document is written:
there are no running or queued qoder tasks, no qoder-owned background
processes and no frontend-owned device services.

## Current task and exact status

- Latest completed round: **Frontend R032** — independent audit of the stable
  R031 F11H candidate. R032 result:
  `coordination/_relay/round-032-frontend-result.md`; evidence:
  `docs/evidence/r032-r031-candidate-audit-20261018.md`.
- Status: **one-module diagnostic candidate passes** the corrected
  independent bitstream/VST/canonical validator, wrapper/container/request
  checks and host LLVM grammar decode. **Production readiness is not
  claimed**, and no compiler/pixel/TCC/input acceptance is claimed.
- Backend R031 was still Working with no handover at the last check and was
  never messaged or interrupted. Its files were stable before/after the R032
  audit (excluding pre-existing `__pycache__`). Any changed backend artifact
  needs a fresh audit.
- All earlier frontend rounds R001–R032 have handed over. The compiler
  candidate work is a parked diagnostic line; the human asks to switch
  workers now.

## Completed commands and results

R032 independent checks (all read-only, local):

```text
python3 analysis/r032-independent-audit/validate_bitstream.py \
  --original analysis/r026-f11h/orig-F11H.bc \
  --candidate analysis/r031-f11h/r031-candidate.record \
  --target-triple air64-apple-ios19.0.0-macabi
# PASS: triple exact, canonical diffs 0, structure OK, VST 1467/1467

python3 analysis/r032-independent-audit/validate_container.py \
  --converted analysis/r006-mps-f11h/mpsimage.conv.metallib \
  --candidate analysis/r031-f11h/mpsimage.r031-candidate.metallib \
  --module-name F11H --target-triple air64-apple-ios19.0.0-macabi
# PASS: 1825 records, module SHA 5d805ec5…, exact triple, delta -448

xcrun clang -x ir -S -emit-llvm analysis/r031-f11h/r031-candidate.record -o /tmp/out.ll
# exit 0 (host Apple clang 17 grammar only; it overrides the module triple)
```

R031 validator/matrix (exit 0), host decode matrix and failed R030 negative
are preserved under `analysis/r031-independent-audit/`. Key result: the R030
splice was grammar-invalid because triple growth shifted function blocks by
96 bits while VST `FNENTRY` offsets stayed old; the R031 writer updates those
offsets and passes the corrected checks.

R025 exact 22F82 acquisition was read-only (map, libxpc text, frame-0 bytes,
launchd log); no device writes, no generation, no package/trust/TCC change.
The `xpc-fault` producer/value investigation is parked: the value is not
captured, and no current composite/input causal witness exists.

## Source, package and device modifications

- **Frontend made no source, test, build-input, package, converter or Git
  changes.** Only dated evidence under `docs/evidence/`, relay result notes
  under `coordination/_relay/`, and disposable scripts/raw bytes under
  `analysis/r0xx*` (gitignored).
- R025 was the only device-facing round: read-only SSH acquisitions; no
  remote files written and no services/generations started. R031/R032 were
  entirely offline.
- The working tree contains pre-existing/backend changes (for example
  `libmachook/mac_hooks.m` and coordination state files) that are **not
  frontend changes**. Do not roll them back or attribute them to this worker.
- Do not shut down, roll back or redeploy the device as part of this worker
  switch.

## Preserved candidate artifacts and hashes

R031/R032 candidate (one-module diagnostic):

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| `analysis/r031-f11h/retargeted-F11H.payload.bc` | 6,920 | `8dc40d786b9e82befaa9e2d7166d88dd63ad9d64339cb293616656acf749c269` |
| `analysis/r031-f11h/r031-candidate.record` | 6,944 | `5d805ec5f65c94c94377370659ebd9f8eae71749486b272ba6c4ed68268252cb` |
| `analysis/r031-f11h/mpsimage.r031-candidate.metallib` | 17,293,876 | `7b71a6479ed162bf4cf4af2445978d7bb849b529d502e9254d3ad67006803aae` |
| `analysis/r031-f11h/r031-candidate.type15.data` | 7,184 | `75663758aa1a914e1cd538ec22690ee73d9e0f9df52ea6c43f56032f40d3da1e` |
| `analysis/r031-f11h/roundtrip-orig.payload.bc` | 6,916 | `ce018722f1d9058534380e6fa0aade64cdaef3cbbd0e806d02d769810273ec9b` |
| `analysis/r031-f11h/roundtrip-conv.payload.bc` | 7,372 | `042303be36e32a7c2755ffae074fc66e99bc6260009b3abcb970a5d87167b849` |

Failed/earlier attempts are preserved for provenance:

- R028 candidate: `analysis/r028-f11h/candidate-F11H.record.bc`
  (6,944 B, `2f2de66b…5b1b`, STRTAB-only change; module triple unchanged).
- R030 splice: `analysis/r030-f11h/retargeted-F11H.record.bc`
  (6,960 B, `e76b655c…7f1a`, host grammar-invalid).
- R031 host-gate/failed/roundtrip logs: `analysis/r031-f11h/host-gate/`.

R032 audit evidence/hashes:

- `analysis/r032-independent-audit/validate_bitstream.py`
  `baf021cad0618f1080cfe3a9ecf7a1c90c8567bb18507fad921350f0e7ca76f1`;
- `validate_container.py`
  `4935a8e2df38324b1b17aeceb95e60d9c61a283c2d277caa04425a7aa78bf795`;
- `host-decode-r031.txt` `fddde986…618da02`;
- `container-r031.json` `377414f7…a0ad406`;
- `request-r031.json` `0189d4a5…6ba04f`;
- `candidate_payload.json` `1b4cff33…6450f`;
- `candidate_record.json` `3817d0bf…c38c`;
- `roundtrip_orig.json` `321cc0ea…9060a`;
- `roundtrip_conv.json` `ae211481…66bd`;
- `audit-summary.json` `7e19ed11…30195`;
- `r031-snapshot-before-clean.txt` and `r031-snapshot-after.txt` are
  identical (`1fe9904c…08d23d`), proving the backend files were stable during
  the audit.

Earlier evidence:
`docs/evidence/r031-validator-llvm-consistency-20261018.md` (SHA
`81d4587c…a07c3`), `r030-retarget-independent-audit-20261018.md`,
`r029-f11h-candidate-audit-20261018.md`, `r028-intrinsic-upgrade-review-20261018.md`,
`r027-attribute-wire-audit-20261018.md`, and the R020–R026 notes.

## Candidate limits and remaining risks

- The R031 candidate replaces only the F11H module; the other 1,824 modules
  are byte-identical to converted. It is **one-module diagnostic success**,
  not full-converter or production success.
- Host `Apple clang 17` proves grammar only and overrides the target triple;
  it is not the exact 22F82 target reader. No no-diagnostic-hook target replay
  or real compiler reply exists in this handover.
- The candidate preserves the original intrinsic attribute records (14
  PARAMATTR groups, no wire 71/62/63/61) and has exact target triple, data
  layout, wrapper payload 6,920/trailing 4 zero, and consistent VST module/
  function offsets.
- Writer hardening items remain for production: explicitly reject block code
  width 0/>32, require zero wrapper trailing padding, require an exact
  approved target/profile string (not merely length 28), and optionally
  assert block-length equality in the audit.
- `bcrewrite.py`/`build_r031_artifacts.py` are disposable diagnostics. Do not
  promote them to production without the hardening, full-profile/all-module
  integration, and independent review.
- Backend R031 may have changed files after the R032 snapshot; re-audit any
  final artifact rather than assuming these hashes.
- Prefer recovery of the last known visible wallpaper/Dock/menu bar before
  production integration of compiler candidates. Never treat grammar success
  as compiler or pixel success.
- Parked/unresolved: exact `xpc-fault` string/producer; provider Identifier
  and codesign EXC_GUARD are separate from current compiler work; no
  production acceptance for TCC/input/pixels.

## Reproducible next commands

```sh
cd /Users/ciscohe/Desktop/macPad/analysis/r032-independent-audit
python3 validate_bitstream.py \
  --original /Users/ciscohe/Desktop/macPad/analysis/r026-f11h/orig-F11H.bc \
  --candidate /Users/ciscohe/Desktop/macPad/analysis/r031-f11h/r031-candidate.record \
  --target-triple air64-apple-ios19.0.0-macabi
python3 validate_container.py \
  --converted /Users/ciscohe/Desktop/macPad/analysis/r006-mps-f11h/mpsimage.conv.metallib \
  --candidate /Users/ciscohe/Desktop/macPad/analysis/r031-f11h/mpsimage.r031-candidate.metallib \
  --module-name F11H --target-triple air64-apple-ios19.0.0-macabi
sh run_tests.sh
sh run_host_decode.sh
```

Before any compiler/device step, read `AGENTS.md`, `coordination/_relay/SYNC.md`,
`coordination/_relay/round-032-frontend-result.md`, the R032 evidence above,
and the latest backend result/review files. Run only a bounded backend-owned
no-hook target-reader replay; do not use this frontend audit as compiler
acceptance.

## Rejected hypotheses / do not repeat

- `wire kind 71 = ZeroExt` is false; LLVM16 wire 71 is `nocallback`
  (`ATTR_KIND_NO_CALLBACK`); `ZExt` is 34.
- R028/R029 string replacement changed a STRTAB entry, not
  `MODULE_CODE_TRIPLE`; do not treat its target attribution as a retarget.
- R030's redundant-VBR splice is grammar-invalid; host error
  `can't enter sub-block: current code size is 0` and stale VST FNENTRY
  offsets explain it.
- Do not use 15.6 libxpc misuse text as 22F82 fact; the exact `xpc-fault`
  value is still missing.
- Provider publisher Identifier is not an established cause of dyld/trust/
  dependency failures; codesign EXC_GUARD is a libxpc lightweight diagnostic
  event, not publisher/trust attribution.
- Do not relax validator rules to accept a candidate, strip attributes, patch
  the reader, or claim grammar/textual equality as compiler/pixel success.

## Cleanup / safe-to-close

TaskList is empty; no qoder validators, host decoder, SSH/helper, backend
writer or agents are running. No frontend-owned device service or
generation was started, so no device cleanup is required. The qoder CLI is
safe to close immediately after this handover is saved; all disposable
evidence is preserved on disk.

(handover to codex now)
