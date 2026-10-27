# Backend R012 result — narrow XPC pointer A-key/zero fix, clean package and owned generation

Date: 2026-10-18 (Asia/Hong_Kong). Scope:
`coordination/_relay/round-012-backend-result-task.md`. One narrow source
change at the direct `__xpc_bootstrap_services` call, one clean full build,
one installed package, one owned generation, cleanup. No global
`MSFindSymbol` change, no role gate, no Steam/Metal/TCC/assert/HID change,
no assertion bypass, no fabricated TCC/pixels/input, no shared compiler
restart.

## 1. Outcome

**The narrow fix is implemented, installed and exercised in one owned
generation. tccd/authd/systemstatusd survive the former XPC bootstrap
point, WindowServer is ready with the provider/libmachook maps, and no new
`_xpc_bootstrap_services` crash occurred.** The installed arm64e callsite
contains the intended `pacia1716` (A-key/zero) + `blraaz` sequence. The
exact signed-call execution log inside the fixed services was not captured
(`tccd-system.err`/`systemstatusd.err` have no `XPC_BOOTSTRAP` line, and
arm64 `macwsinteropd.err` still reports `SYMBOL NOT FOUND`), so the result
does not claim a direct signed-call entry witness. Pixels, TCC reply and
accepted input remain separately unaccepted.

## 2. Source change

Frozen HEAD `c97e219`; a clean worktree `/private/tmp/macpad-r012-clean`
was created and the same one-file change was applied to the main tree
(visible to frontend R019) and the build tree.

- `libmachook/mac_hooks.m` only; SHA-256
  `e6cc4597a6a61bf65a25782d4939eb1470c0e237aaf3af40c46fa296eeb9463d`;
  patch `coordination/artifacts/R012-xpc-pointer-signing-20261018.patch`,
  SHA-256 `5567b452…f087`; diff 1 file, 14 insertions, 1 deletion.
- Raw `MSFindSymbol` result stored separately; on `__arm64e__` signed with
  `ptrauth_sign_unauthenticated(raw, ptrauth_key_asia, 0)` before the
  existing call; arm64 keeps the raw cast. Dictionary, setup timing,
  `#### XPC_BOOTSTRAP`/result logs and failure behavior unchanged.
- Provider source `6ac796c7…` unchanged; static contract PASS; only one
  source file changed.

## 3. Compiled and installed callsite

Compiled arm64e sequence (built and installed bytes identical):

```text
0x6a10/0x6a1c  bl MSFindSymbol
0x6a20          cbz x0
0x6a24          mov x17, x0
0x6a28          mov x16, #0
0x6a2c          pacia1716          ; A-key, modifier 0
0x6a30          mov x21, x17
0x6a50          cbz x21
0x6a54          mov x0, x20         ; unchanged dictionary
0x6a58          blraaz x21
0x6a5c          return
```

Built range `0x69ec..0x6a60` SHA-256
`4c06e5f57d691255d37ad3d7c3afd4b002c57890c25d5f6741ca8b10128390d9`;
the installed thin arm64e range is byte-identical. Installed libmachook:
arm64e SHA `47f78880…d9432`, UUID `C439DBEC-1C83-3EB7-A717-07F380024A4B`,
CDHash `b579715e…`; arm64 SHA `6de9b688…cbd18`, UUID
`957B5055-CF61-35F5-AE63-7F5E3BB6DDC6`, CDHash `0bbcbb6f…`; both
`@rpath/CydiaSubstrate.framework/CydiaSubstrate`.

## 4. Build, package and install

- Build exit **0** from the clean tree; reused the validated R010
  MacWSWindowing artifact (`51222343…8cc`, manifest verified); package
  `com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb` SHA-256
  `c0b9fb22a15b8cb53fb741ddc1353e58cc858e8aa924154dcc4d77d331354176`,
  6,047,170 bytes, 273 members; artifact-contract verify PASS.
- Staged provider source unchanged; clean rebuild produced new binary
  identities: main `992b70d3…c322`, arm64 `2ad4c652…433a`, arm64e
  `6dfa858e…4326`; both slices platform 1/minos 13.0.0/exact install name.
  Staged libmachook fat `56a5202d…169b6`.
- Device install `dpkg -i` exit **0**; package `0.3.4` remains installed.
  Published provider target inode **250671913**, SHA-256
  `b2ae21da42704b1d584f2b655e837f7db62a10f2a035b3f741adc819ce1423e7`,
  arm64 UUID `7284A084-5EBA-3D4B-987A-28D5C6CA1D31`, arm64e UUID
  `5700E649-B271-36CF-888C-4CCC8E1BA6EC`; CDHashes
  `88ce9304…`/`4218d9f1…`, both trustcache-live; both slices exact.
  Secondary: ldid Identifier remains the publisher temp basename
  `.CydiaSubstrate.new-75235`.
- R010 baseline preserved at
  `/var/mobile/macws_r012_evidence_20261018/r010-baseline/` (provider
  `7320a210…`, libmachook `4fa53411…`/`d2f110fd…`, rootfs copies); the R010
  package remains in its evidence directory.

## 5. One owned generation

`macos_gui.sh start coexist --no-terminal --no-vnc` exit **0** in 66 s.
Native SpringBoard 872 / tccd 883 / backboardd 905 stayed alive; thermal
nominal 33.79 °C; markers absent.

- tccd-system 78174: protocol round-trip ready; authd 78162 and
  systemstatusd 77896 alive; no tccd/authd/systemstatusd crash report.
- WindowServer 78380: process ready, stable 8 s; macwsinputd 78372,
  macwsdisplayd 78476.
- No new `_xpc_bootstrap_services` crash. WS map probe on 78380 found 8
  mappings including
  `/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` and
  `/usr/local/lib/libmachook_arm64.dylib` (exit 0). Provider method probe:
  provider path above, UUID `5700E649…`, `VERDICT=INSTALLED-ORIGINAL-CALLABLE`,
  exit 0.
- Exact signed-call execution log is missing: `tccd-system.err` 927 B and
  `systemstatusd.err` 948 B contain constructor/permit lines only, no
  `#### XPC_BOOTSTRAP`; arm64 `macwsinteropd.err` still logs `fn=0x0` and
  `SYMBOL NOT FOUND`. The services survive and the callsite is exact, but a
  direct signed-call entry witness is not claimed.
- Final composite `state=missing producer=0 sequence=0` (reason
  `displayd-start`); no pixels. TCC reply and accepted input were not
  re-tested; R008 baselines preserved. R016 reopen freshness was not run
  (live WS existed, but no G0/G1/same-PID witness).
- New crash delta: 15 `ExcUserFault_*` reports at 19:32–19:33, all
  `EXC_GUARD`/`GUARD_TYPE_USER`, namespc 7, reason 9 (pkd, SystemUIServer,
  OSXvnc-server, filecoordinationd, coreaudiod, QuickLookUIService,
  ViewBridgeAuxiliary, macwsworkspacectl, locationd, others). The listed
  processes were alive in the status; these are not the former XPC
  bootstrap crash and were not attributed/fixed here.

## 6. Cleanup and final state

`macos_gui.sh stop` exit **0**; no WindowServer/project tccd-system/
displayd/inputd/interopd/iconservicesagent/launchdchrootexec/probe process
remains. Native SpringBoard 872 / tccd 883 / backboardd 905 remain; markers
absent in both namespaces. The R012 package and provider target remain
installed; the R010 baseline copies and probes are preserved in
`/var/mobile/macws_r012_evidence_20261018/`. The pre-existing MacWSHost
71324 recorded in R011 was absent in the R012 pre-start inventory; R012 did
not kill it and created no new Host app. The only source change is the one
authorized `mac_hooks.m` diff (uncommitted in the main tree for coordinator/
frontend integration); no unrelated WIP.

## 7. Limits / next step

The fix is installed and the generation is restored, but the exact signed
XPC call execution inside tccd/authd/systemstatusd is not directly logged;
a future bounded process-local XPC bootstrap log in an arm64e service would
close that witness. The new `ExcUserFault` guard reports need separate
review. Pixels, TCC reply and accepted input remain unaccepted; the provider
publisher temp ldid Identifier remains secondary. Do not broaden into
pixel/TCC/input repair, pacing or compiler changes from this result.

Evidence: `docs/evidence/r012-xpc-pointer-signing-20261018.md`,
`coordination/artifacts/R012-xpc-pointer-fix-20261018.json`, and ignored
`analysis/r012-xpc-fix/` (patch/static check, staged/installed callsite
disassembly, build/package/install/generation/service/cleanup logs and
ExcUserFault reports).

(handover to codex now)
