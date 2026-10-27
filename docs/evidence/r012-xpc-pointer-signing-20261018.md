# R012 — narrow XPC function-pointer A-key/zero fix, package and owned generation

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-012-backend-result-task.md`. One narrow source
change at the direct `__xpc_bootstrap_services` call; one clean full build;
one installed package; one owned generation. No global `MSFindSymbol`
change, no role gate, no Steam/Metal/TCC/assert/HID change, no assertion
bypass, no fabricated TCC/pixels/input, no shared compiler restart.

## 1. Source change and static scope

Frozen HEAD `c97e219`. A clean detached worktree
`/private/tmp/macpad-r012-clean` was created at that commit; the same
one-file change was applied to the main tree (visible to frontend R019) and
the clean build tree.

| Item | Value |
|---|---|
| changed file | `libmachook/mac_hooks.m` only |
| source SHA-256 | `e6cc4597a6a61bf65a25782d4939eb1470c0e237aaf3af40c46fa296eeb9463d` |
| patch | `coordination/artifacts/R012-xpc-pointer-signing-20261018.patch` |
| patch SHA-256 | `5567b452df17c5720ef08c54fc95768cd71e3377a099caf8e4e7ad3a6f03f087` |
| diff | 1 file changed, 14 insertions(+), 1 deletion(-) |
| provider source | `6ac796c7…aa7c2b`, unchanged |
| static contract | PASS (raw lookup, A-key/zero sign, call/dict/log/failure preserved, one source file) |

The call now separates the raw lookup and signs only the direct XPC call:

```c
void *_xpc_bootstrap_services_raw =
    MSFindSymbol((MSImageRef)header, "__xpc_bootstrap_services");
int(*_xpc_bootstrap_services_fn)(xpc_object_t) = NULL;
if (_xpc_bootstrap_services_raw) {
#if __arm64e__
    _xpc_bootstrap_services_fn = (int(*)(xpc_object_t))
        ptrauth_sign_unauthenticated(_xpc_bootstrap_services_raw,
                                     ptrauth_key_asia, 0);
#else
    _xpc_bootstrap_services_fn = (int(*)(xpc_object_t))
        _xpc_bootstrap_services_raw;
#endif
}
```

The dictionary, setup timing, `#### XPC_BOOTSTRAP` logging, result logging
and null/failure behavior are unchanged. `MSFindSymbol` and every other
callsite are untouched.

## 2. Build and package

- Clean full build from the R012 worktree; reused the validated R010
  Apple-ld64 MacWSWindowing artifact (`51222343…8cc`, manifest verified
  against the R012 tree); host Xcode/Theos/GNU Make/lzma/dpkg-deb shims.
- Build exit **0**; package
  `packages/com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb`, SHA-256
  `c0b9fb22a15b8cb53fb741ddc1353e58cc858e8aa924154dcc4d77d331354176`,
  6,047,170 bytes, 273 payload members.
- `macws_artifact_contract.py verify-package` PASS on the real archive.
- Staged provider (source unchanged; clean rebuild produced new binary
  UUIDs/hashes, not a source change): main `992b70d3…c322`, arm64
  `2ad4c652…433a`, arm64e `6dfa858e…4326`; both slices platform 1,
  minos 13.0.0, exact install name.
- Staged libmachook fat `56a5202d60dda16b5dff3a3defa4445f01e7d0e65ad67ad26c7efd39667169b6`,
  3,040,496 bytes.

## 3. Compiled and installed callsite bytes

The arm64e slice of the clean build contains the exact A-key/zero contract:

```text
0x6a10/0x6a1c  bl MSFindSymbol
0x6a20          cbz x0
0x6a24          mov x17, x0
0x6a28          mov x16, #0
0x6a2c          pacia1716          ; PACIA x17, x16 = key A, modifier 0
0x6a30          mov x21, x17
0x6a50          cbz x21
0x6a54          mov x0, x20         ; unchanged dictionary
0x6a58          blraaz x21
0x6a5c          return
```

Built range `0x69ec..0x6a60` SHA-256
`4c06e5f57d691255d37ad3d7c3afd4b002c57890c25d5f6741ca8b10128390d9`.
After install, the same range read from the installed thin arm64e library is
**byte-identical**; the installed callsite contains `pacia1716` with x16=0
and `blraaz x21`.

Installed libmachook:

| Item | Value |
|---|---|
| arm64e | SHA `47f78880f6de1fadc9faabbf79d16ccc56cfba2feb5a45e2155e09c22c2d9432`; UUID `C439DBEC-1C83-3EB7-A717-07F380024A4B`; CDHash `b579715e…` |
| arm64 | SHA `6de9b6880495c3e0f431c2fc0e328b4420aecac178391d466fa71b141efcbd18`; UUID `957B5055-CF61-35F5-AE63-7F5E3BB6DDC6`; CDHash `0bbcbb6f…` |
| load command | `@rpath/CydiaSubstrate.framework/CydiaSubstrate` |

## 4. Install and provider read-back

Remote `.deb` SHA matched; `dpkg -i` (with `MACWS_POSTINST_NO_RESPRING=1`)
exit **0**; package remains `com.kdt.macosbooter 0.3.4 install ok installed`.
Published provider target: inode **250671913**, SHA-256
`b2ae21da42704b1d584f2b655e837f7db62a10f2a035b3f741adc819ce1423e7`,
fat arm64+arm64e, both slices platform 1/minos 13.0.0/exact install name.

| Provider item | Value |
|---|---|
| arm64 UUID | `7284A084-5EBA-3D4B-987A-28D5C6CA1D31` |
| arm64e UUID | `5700E649-B271-36CF-888C-4CCC8E1BA6EC` |
| arm64 CDHash | `88ce9304d855233b4a945890b67e31f0c4fc93aa` |
| arm64e CDHash | `4218d9f14e3b52fc1d71ae33af2385ac930ce713` |
| trustcache | both live |
| ldid Identifier | `.CydiaSubstrate.new-75235` (publisher temp basename; secondary finding) |

R010 baseline preserved at
`/var/mobile/macws_r012_evidence_20261018/r010-baseline/`: provider
`7320a210…`; libmachook `4fa53411…`/`d2f110fd…`; rootfs copies. The R010
package `.deb` remains in the R010 evidence directory.

## 5. One owned generation

`macos_gui.sh start coexist --no-terminal --no-vnc` exit **0** in 66 s;
native SpringBoard 872 / tccd 883 / backboardd 905 stayed alive; thermal
nominal 33.79 °C; markers absent.

- tccd-system pid **78174**: protocol round-trip ready.
- authd pid **78162**, systemstatusd pid **77896**: alive.
- WindowServer pid **78380**: process ready, stable for 8 s; graphics not
  yet witnessed.
- macwsinputd **78372**, macwsdisplayd **78476**.
- No new `_xpc_bootstrap_services`, tccd, authd or systemstatusd crash
  report. The former R010 failure points survived.
- WS map probe (pid 78380): 8 mappings, including
  `/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` and
  `/usr/local/lib/libmachook_arm64.dylib`; exit 0.
- Provider method probe: provider path above, UUID `5700E649…`, `via-hook=43
  replacement_called=1 original_called=1`, `VERDICT=INSTALLED-ORIGINAL-CALLABLE`,
  exit 0.

The exact signed-call execution in the fixed services was **not directly
logged**: `tccd-system.err` (927 B) and `systemstatusd.err` (948 B) contain
the constructor/permit lines but no `#### XPC_BOOTSTRAP` line;
`macwsinteropd.err` (arm64) still logs `fn=0x0 … SYMBOL NOT FOUND`. The
services survive and no former crash occurs, and the installed callsite is
exact, but the result does not claim that the signed call entered libxpc in
those processes. A direct signed-call execution witness remains missing.

## 6. Separate acceptance layers and crash delta

- Final composite: `state=missing producer=0 sequence=0`,
  `reason=displayd-start`; **no pixels** claimed.
- TCC reply: not re-tested; R008 empty-dictionary/attribution baseline
  preserved.
- Accepted input: not re-tested; R008 postmin 1002 baseline preserved.
- R016 reopen freshness: not run; the live WS existed but no G0/G1/same-PID
  reopen witness was produced.
- New crash delta: 15 `ExcUserFault_*` reports at 19:32–19:33, all
  `EXC_GUARD`/`GUARD_TYPE_USER`, namespc 7, reason 9 (pkd, SystemUIServer,
  OSXvnc-server, filecoordinationd, coreaudiod, QuickLookUIService,
  ViewBridgeAuxiliary, macwsworkspacectl, locationd, and others). The listed
  processes were alive in the status; these are not the former
  `_xpc_bootstrap_services` crash and were not attributed or fixed here.
  They are preserved as a separate delta.

## 7. Cleanup and final state

`macos_gui.sh stop` exit **0**; no remaining WindowServer/project
tccd-system/displayd/inputd/interopd/iconservicesagent/launchdchrootexec/
probe process. Native SpringBoard 872 / tccd 883 / backboardd 905 remain.
Markers absent in both namespaces. The R012 package remains installed and
the published provider inode 250671913 remains; the R010 baseline copies
and staged probes are in `/var/mobile/macws_r012_evidence_20261018/`. The
pre-existing MacWSHost app PID 71324 recorded in R011 was absent in the
R012 pre-start inventory; R012 did not kill it, and no new Host app was
created. No source/build/package/TCC/input changes remain beyond the one
authorized source diff and the installed R012 package.

## 8. Conclusion

The narrow A-key/zero signing fix is implemented and installed; the
installed arm64e callsite contains `pacia1716` (x16=0) followed by
`blraaz x21`; the package builds/installs; the former XPC point no longer
crashes; tccd/authd/systemstatusd survive; WindowServer is ready with the
provider/libmachook maps. The exact signed-call execution log and the
pixels/TCC/input acceptance remain missing/separate. The next bounded step,
if needed, is a process-local XPC bootstrap execution witness in an
arm64e service; do not broaden into pixel/TCC/input acceptance or
pacing/compiler changes.

Evidence: `coordination/artifacts/R012-xpc-pointer-fix-20261018.json`,
`analysis/r012-xpc-fix/` (patch, static check, staged/installed callsite
disassembly, build/package/install/generation/service/cleanup logs,
ExcUserFault reports).

(handover to codex now)
