# Backend R010 result — merged provider/F1 package built, installed, owned-generation acceptance blocked

Date: 2026-10-18 (Asia/Hong_Kong). Scope:
`coordination/_relay/round-010-backend-result-task.md`. Frozen candidate
`c97e219` built once from the clean `/private/tmp/macpad-r010-clean`
worktree, verified, installed, probed and cleaned up. No source change,
commit, rollback or concurrent rebuild. Credentials stayed transient.

## 1. Outcome

**The package builds, verifies and installs; the provider/F1 standalone
contracts pass; the merged package is NOT accepted as a working generation.**
The owned WindowServer generation failed because the new `libmachook`
crashes tccd/authd/systemstatusd at `_xpc_bootstrap_services` with a PAC
fault (the LINKEDIT fix now resolves the local symbol; the unconditional
call from `loadImageCallback`/`InitStuff` is not PAC/ABI-safe). The exact
missing runtime witness is a live WindowServer generation and its
loaded-provider map.

## 2. Freeze and source identity

- HEAD `c97e219f3f367662dc9bdb1f20d7e76fba544df1`; contains
  `3c4d581` (provider/LINKEDIT/method), `4bcf20d` (F1), `b717fb9`/`c97e219`
  (R012 package). Clean worktree HEAD `c97e219`, zero dirty paths.
- Main tree source diff empty outside coordination; no stash/reset/checkout/
  source sync. No unrelated WIP used.
- Key source hashes: `misc/cydiasubstrate_macos.m` `6ac796c7…aa7c2b`,
  `include/macws_app_input_route.h` `29d7e17d…6440d`,
  `libmachook/mac_hooks.m` `e8ae9dad…b08d`,
  `libmachook/Metal_hooks.x` `a49c52c1…91e4a`,
  `libmachook/AppInputBridge.m` `e794623b…32b`.

## 3. Build and package verification

Device Theos is absent, so the recorded host cross-build path was used:
Xcode 26.2 clang 17.0.0/Apple ld64, Theos `/Users/ciscohe/theos`, GNU Make
3.81 via a local `gmake` shim, and local Python `lzma`/`dpkg-deb` shims.
The Apple-ld64 MacWSWindowing artifact was built and verified (`sha256
51222343e4b15c225a2741ce3a0f5b8460725056f6fe213addc5f2986a8bc0cc`,
cf auth=253/plain=0, rootless substrate load command; manifest verify
passed). Full package build exit **0**:

| Item | Value |
|---|---|
| package | `packages/com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb` |
| SHA-256 | `b80d57669a948f9589c081a39f21a14ccf7b8ccdccb92dce6cde9097a798d66a` |
| size/members | 6,042,198 bytes; 273 payload members |
| artifact contract | `verify-package` PASS against the real `.deb` (provider main/both companions/publisher scripts included) |
| provider main | `ca587894a78ba13f55b1a738dc0ffdb18fe16b73e595b7055a994bc23110dabc` |
| provider arm64 | `58b62f063d2872bdda3c2e1408f9f9c9b1804953b297a8c1043603cda48b8393` |
| provider arm64e | `b9e6a4c2b464de0f557288fb898838d9ab3c2c1450668965fe2ac008cc625ba1` |
| staged provider slices | both platform 1, minos 13.0.0, install name `/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` |
| staged libmachook | `c9fa38252aeadd8b7627238cb395a83743dea0dccb891c8d1b0bb9fe5cd6844e`, 3,040,496 bytes |

## 4. Install and provider read-back

Remote `.deb` SHA matched; `dpkg -i` (with `MACWS_POSTINST_NO_RESPRING=1`)
exit **0**. Package remains `com.kdt.macosbooter 0.3.4 install ok installed`.
The postinst published and trusted the provider:

- Target `/var/mnt/rootfs/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`,
  inode **250668640** (baseline 250315582), SHA-256
  `7320a210c3e85963ce149398e9bb0b0a34e7c1337ed9af1dbd77b0a328477337`,
  152,688 bytes, fat arm64+arm64e.
- UUIDs arm64 `B9199E06-3CE6-3116-80DB-5AA8B9DED5A5`, arm64e
  `A755DD35-D80C-3D90-9312-D566DE392770`; both match package companions.
  Slices platform 1, minos 13.0.0, exact install name.
- CDHashes arm64 `7df54d9c6155ab46943c9dda4ef92852498ab60a`, arm64e
  `464c571b1b08d7c467b7316aaeb46a41a8f9c7a0`; both live in trustcache.
- Package-owned provider: main inode 123147/`ca587894…`, arm64 inode
  123148/`58b62f06…`, arm64e inode 123149/`b9e6a4c2…`.
- Installed libmachook: `libmachook.dylib` inode 123625 SHA
  `4fa53411d6477720cdfb4a540f40fa1c63ce020ff093576fa6732dd3da399322` CDHash
  `95f1f023…`; `libmachook_arm64.dylib` inode 123627 SHA
  `d2f110fda66a4414fe926193b859434d9d18fb6f8911c8c0c9e28291cd95d4cd` CDHash
  `c7b2aaee…`; both `@rpath/CydiaSubstrate.framework/CydiaSubstrate`.
- Secondary finding: the published target's ldid Identifier is the
  publisher temp basename `.CydiaSubstrate.new-67705`, not
  `CydiaSubstrate`; the code signature/CDHash and load path are otherwise
  valid.

## 5. Provider method and F1 runtime

The old provider (baseline) was a method no-op: `via-hook=42,
replacement_called=0`, `VERDICT=PARTIAL`, UUID `EC16F01C-5FE7-3D57-B0F1-
3724BDC2E273`; old WS 50574 mapped the provider and `libmachook_arm64`.

After install, both thin probes loaded the published provider at
`/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` with
UUID `A755DD35-D80C-3D90-9312-D566DE392770` and produced:

```text
fixture-before=42
original-direct=42
via-hook=43 replacement_called=1 original_called=1
VERDICT=INSTALLED-ORIGINAL-CALLABLE  (arm64 probe exit 0; arm64e probe exit 0)
```

Both probes selected the **arm64e** provider slice; the distinct arm64
runtime slice was not separately observed (the on-disk arm64 slice
`B9199E06…` is verified/trusted). The F1 device probe from the frozen header
passed **9/9** cases (configure/close with global flag -> RejectGlobalOwner;
zero window -> RejectWindowRequired; global pointer -> GlobalSystemSurface;
Dock SystemGesture -> global, non-Dock -> reject; relative pointer
flag/zero -> reject). This is a contract execution, not a live
AppInputBridge/WS production route; no production-safe trigger was used, so
a live F1 route witness remains missing and no input acceptance is claimed.

## 6. Owned-generation failure (blocking)

```text
macos_gui.sh restart coexist --no-terminal --no-vnc -> exit 1
minimal path: tccd protocol round-trip failed status=4;
              TCCPING timeout service=com.apple.tccd.system after 5s
fallback:     systemstatusd did not start; GUI stopped and iOS restored
WS PID after: none; WindowServer provider map: missing
```

New crash reports at 18:26 share one crash:

```text
tccd-2026-10-18-182623.ips (pid 70481)
authd-2026-10-18-182623.000.ips (pid 70469)
systemstatusd-2026-10-18-182649.ips (pid 70814)

EXC_BAD_ACCESS / SIGSEGV
KERN_INVALID_ADDRESS at 0x2000000180ae1140 -> 0x0000000180ae1140
(possible pointer authentication failure)

_xpc_bootstrap_services +0xf140
loadImageCallback +0x6a4c
dyld4 add-image block ... InitStuff
image: /usr/local/lib/libmachook.dylib UUID 76a98a5a-0c9c-32e5-93a3-b006f2bcd6d4
```

This is the exact R007-audit risk: the LINKEDIT fix resolves the local
`__xpc_bootstrap_services`; the unconditional call from
`loadImageCallback`/`InitStuff` is not PAC/ABI-safe. It is a candidate
regression, separate from the provider/F1 fixes. The failed restart's
tccd/authd/systemstatusd are project/chroot services; native tccd 883,
SpringBoard 872 and backboardd 905 stayed alive. An install-time
`ExcUserFault_codesign` (EXC_GUARD namespc=7 reason=9) was also recorded;
`dpkg -i` still exited 0.

## 7. Separate endpoints

| Endpoint | Result |
|---|---|
| Package build/archive/provider contract | PASS |
| Install/provider publish/trust | PASS |
| Loaded provider + method replacement/original | PASS in chroot probes; WS mapping missing |
| F1 route contract | PASS 9/9 contract; live production route missing |
| WindowServer process readiness | **FAIL** (tccd status=4; systemstatusd failed; no WS) |
| Final-composite sequence/pixels | `state=missing producer=0 sequence=0`; no pixels |
| TCC reply | not re-tested; R008 empty-dictionary/attribution-failure baseline preserved |
| Accepted input | not re-tested; R008 postmin 1002 baseline preserved |

## 8. Cleanup and final state

- `macos_gui.sh stop` exit **0**; failed generation had already restored iOS.
  A leftover Host app PID 70969 from the failed generation was TERM-stopped;
  no MacWSHost/WindowServer/displayd/inputd/interopd/agent/
  launchdchrootexec/probe process remains.
- Native SpringBoard 872 / native tccd 883 / backboardd 905 alive.
- Markers absent in both namespaces; no TCC policy/db/cache edit.
- Package remains installed; published provider remains `7320a210…` with
  both CDHashes trustcache-live. No rollback was performed. Baseline
  provider/libmachook copies are preserved at
  `/var/mobile/macws_r010_evidence_20261018/baseline/`; probes at
  `/var/mobile/macws_r010_evidence_20261018/probes/`.
- Thermal: `macos_gui.sh status` nominal, 34.59 °C; sysctl unreadable.

## 9. Recommendation

Do not accept this merged candidate as a working generation. In a follow-up
packet, narrowly gate the `libmachook` `_xpc_bootstrap_services` call by
role/identity or correct its PAC/ABI contract, rebuild, and repeat the owned
generation/WS provider map. Also fix the provider publisher's temp
code-sign Identifier. The provider package/publisher and method/F1
contracts themselves behave correctly in the standalone probes; the missing
witness is the WindowServer generation. Pixels, TCC reply and input remain
separate and unaccepted.

Evidence: `docs/evidence/r010-merged-provider-f1-package-20261018.md`,
`coordination/artifacts/R010-provider-f1-package-20261018.json`, and ignored
`analysis/r010-provider-f1/` (build/package/install/probe logs and the
tccd/authd/systemstatusd/codesign crash reports).

(handover to codex now)
