# Backend R031 result — canonical writer, gates passed, replay pending

Canonical LLVM-16 bitstream re-encoder only under `analysis/r031-f11h`;
no compiler/private-endpoint replay. No production
compiler/WS/generation/package/source/Git/policy/marker change.
Credential transient.

## Gate results

- Unmodified-original roundtrip is **byte-identical** for both preserved
  F11H payloads: original 6,916/`ce018722…ec9b`, converted
  7,372/`042303be…b849`.
- Candidate uses the exact 28-byte `air64-apple-ios19.0.0-macabi`.
  MODULE_CODE_TRIPLE 303→351 bits, module words 1,465→1,466, VST start
  46,912→46,944, `VSTOFFSET` 1,466→1,467, function blocks +32 bits,
  FNENTRY 1,093/1,105/1,352→1,094/1,106/1,353. No redundant VBR chunks,
  no splices, no llvm-dis/as roundtrip, no attribute change.
- Independent decoded-record audit: 21 blocks, 37 abbreviations, 476
  records, 0 differences (only triple/VSTOFFSET/FNENTRY derived values).
- Host LLVM 17: `llvm-nm` exit 0 on both; `clang -cc1 -emit-llvm-bc`
  (full materialize+rewrite) exit 0 on both; text IR diff is only
  `ModuleID`.
- Device LLVM-16 `llvm-dis`: exit 0 for original payload, candidate
  payload and candidate wrapped record; diff is only `ModuleID` plus
  `target triple = "air64-apple-macosx13.4.0"` →
  `"air64-apple-ios19.0.0-macabi"`.

## Artifacts

| Artifact | Bytes | SHA-256 |
|---|---|---|
| `analysis/r031-f11h/bcrewrite.py` | — | `9b34ab9742807e434c5a284a91c18e60ee4b81ef2222f4a4e2901395bd10e4e5` |
| candidate payload | 6,920 | `8dc40d786b9e82befaa9e2d7166d88dd63ad9d64339cb293616656acf749c269` |
| candidate record | 6,944 | `5d805ec5f65c94c94377370659ebd9f8eae71749486b272ba6c4ed68268252cb` |
| candidate type-15 data | 7,184 | `75663758aa1a914e1cd538ec22690ee73d9e0f9df52ea6c43f56032f40d3da1e` |
| candidate metallib | 17,293,876 | `7b71a6479ed162bf4cf4af2445978d7bb849b529d502e9254d3ad67006803aae` |

Control remains R028 `type15.control.data` 7,632/`ffa7daa5…e90`.

## Not run

The one bounded private control/candidate replay was not executed: the
human switch request arrived first and forbids new experiments. No
service or endpoint was started; the live `MacWS/r014` path is absent.

Next exact steps (full detail in
`docs/evidence/r031-canonical-retarget-20261018.md` §6):
restore `owned/r014` from
`/var/mobile/macws_r028_evidence_20261018/` to the hardcoded live path,
replay control then `r031-candidate.type15.data` with
`r028_replay_client` + `r014_redirect_v6.dylib`, and on real success
re-run without the R024 hook before claiming anything.

## Corrections

R028/R029/R030 in-place string replacement touched the STRTAB blob
string, not MODULE_CODE_TRIPLE. The R028 `Target OS is incompatible.`
reply did not come from a changed module triple, and the target-OS
inference is withdrawn.

## Cleanup and state

- Device used: `192.168.68.63:2222` (Cs-Pad-2, iPad13,11); the most
  recent `.62:2222` endpoint was unreachable. Only a new evidence dir
  `/var/mobile/macws_r031_evidence_20261018/temp/` (7 files: 4 inputs,
  3 `llvm-dis` outputs) was created. No trustcache addition.
- No owned process; live endpoint absent; all six markers absent.
- Native SpringBoard 872 / tccd 883 / backboardd 905 unchanged; package
  0.3.4; provider `b2ae21da…`, libmachook `47f78880…`/`6de9b688…`
  unchanged; production compiler count 15 before/after (was already 15
  this round; R028 recorded 14).
- Unrelated system crash activity (macwsdisplayd/inputd/interopd/
  UIKitSystem 22:28-22:33 and a production-path MTLCompilerService crash
  22:31:29) is recorded as an observation; no private endpoint existed,
  so it is not attributable to this round. Recover the visible
  wallpaper/Dock/menu bar state before any further compiler work.

Evidence: `docs/evidence/r031-canonical-retarget-20261018.md`,
`analysis/r031-f11h/`, device
`/var/mobile/macws_r031_evidence_20261018/`.

(handover to codex now)
