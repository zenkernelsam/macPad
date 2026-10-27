# R010 — merged provider/F1 package install and blocked owned-generation acceptance

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-010-backend-result-task.md`. This was the single
build/deploy/device lane for the frozen candidate. The package was built,
verified and installed. The provider package and the repaired method/F1
contracts pass in disposable chroot probes, but the merged package's
`libmachook` crashes tccd/authd/systemstatusd at `_xpc_bootstrap_services`
with a PAC fault, so a clean owned WindowServer generation could not be
established. The candidate is therefore **not accepted as a working
generation**; the exact missing witness is a live WindowServer generation
with its loaded-provider map.

## 1. Freeze and source identity

- HEAD: `c97e219f3f367662dc9bdb1f20d7e76fba544df1`.
- Contains: `3c4d581` (provider/LINKEDIT/method fixes), `4bcf20d` (F1
  exact-window/global rejection), `b717fb9`/`c97e219` (R012 package).
- Clean detached worktree: `/private/tmp/macpad-r010-clean`, HEAD
  `c97e219`, `git status --porcelain` empty.
- Main tree source diff was empty outside coordination; no
  stash/reset/checkout/source synchronization. Untracked relay/docs were
  not used by the build.
- Source hashes: `misc/cydiasubstrate_macos.m`
  `6ac796c7…aa7c2b`, `include/macws_app_input_route.h`
  `29d7e17d…6440d`, `libmachook/mac_hooks.m`
  `e8ae9dad…b08d`, `libmachook/Metal_hooks.x`
  `a49c52c1…91e4a`, `libmachook/AppInputBridge.m`
  `e794623b…32b`.

## 2. Build and package verification

Device Theos is absent, so this used the same host cross-build path recorded
in R008: Xcode 26.2 clang 17.0.0/Apple ld64, Theos
`/Users/ciscohe/theos`, GNU Make 3.81 through a local `gmake` shim, and
local Python `lzma`/`dpkg-deb` shims for `dm.pl` and the artifact contract.

The Apple-ld64 MacWSWindowing artifact was built and independently verified:

```text
sha256 51222343e4b15c225a2741ce3a0f5b8460725056f6fe213addc5f2986a8bc0cc
size 404080; __cfstring auth-bind/key=DA=253; plain-bind=0
no /Library load commands; @rpath/CydiaSubstrate.framework/CydiaSubstrate
MacWS artifact contract: verify passed
```

The full package was built once from the clean worktree:

```text
gmake FINALPACKAGE=1 STRIP=0 OPTFLAG=-O2 THEOS_PACKAGE_SCHEME=rootless \
  GO_EASY_ON_ME=1 MACWS_WINDOWING_CROSS_PREBUILT=<artifact> package
exit 0
packages/com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb
sha256 b80d57669a948f9589c081a39f21a14ccf7b8ccdccb92dce6cde9097a798d66a
size 6042198; 273 payload members
```

`macws_artifact_contract.py verify-package` passed on the real archive
(using the local `dpkg-deb` shim), including the provider main, both thin
companions and the two publisher scripts. Staged provider:

| Item | SHA-256 |
|---|---|
| fat main | `ca587894a78ba13f55b1a738dc0ffdb18fe16b73e595b7055a994bc23110dabc` |
| arm64 companion | `58b62f063d2872bdda3c2e1408f9f9c9b1804953b297a8c1043603cda48b8393` |
| arm64e companion | `b9e6a4c2b464de0f557288fb898838d9ab3c2c1450668965fe2ac008cc625ba1` |

Both staged slices: platform 1 (macOS), minos 13.0.0, install name
`/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`.
Staged fat `libmachook.dylib`: `c9fa3825…6844e`, 3,040,496 bytes.

## 3. Install and provider read-back

The `.deb` was uploaded and the remote SHA-256 matched the host SHA.
`dpkg -i` (with `MACWS_POSTINST_NO_RESPRING=1`) exited **0**; the package
remains `com.kdt.macosbooter 0.3.4 install ok installed`.

The postinst published the provider to the mounted rootfs:

| Item | Value |
|---|---|
| target | `/var/mnt/rootfs/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` |
| inode | 250668640 (old baseline 250315582) |
| SHA-256 | `7320a210c3e85963ce149398e9bb0b0a34e7c1337ed9af1dbd77b0a328477337` |
| size | 152688; fat arm64 + arm64e |
| UUIDs | arm64 `B9199E06-3CE6-3116-80DB-5AA8B9DED5A5`; arm64e `A755DD35-D80C-3D90-9312-D566DE392770` |
| CDHashes | arm64 `7df54d9c6155ab46943c9dda4ef92852498ab60a`; arm64e `464c571b1b08d7c467b7316aaeb46a41a8f9c7a0` |
| trustcache | both live (indices 42/43 and 24/25) |
| slices | both platform 1, minos 13.0.0, exact install name |
| ldid Identifier | `.CydiaSubstrate.new-67705` — publisher temp basename, secondary finding |

The target UUIDs match the package companions; the target is the companion
code reconstructed and re-signed by the publisher, so whole-file/slice bytes
differ by the signature. Package-owned provider files:
main inode 123147/`ca587894…`, arm64 inode 123148/`58b62f06…`, arm64e inode
123149/`b9e6a4c2…`.

Installed libmachook:

| Item | Value |
|---|---|
| `/var/jb/usr/macOS/lib/libmachook.dylib` | inode 123625, SHA `4fa53411d6477720cdfb4a540f40fa1c63ce020ff093576fa6732dd3da399322`, CDHash `95f1f023d12b2f05dfd03793c6e56a5b56630268` |
| `/var/jb/usr/macOS/lib/libmachook_arm64.dylib` | inode 123627, SHA `d2f110fda66a4414fe926193b859434d9d18fb6f8911c8c0c9e28291cd95d4cd`, CDHash `c7b2aaee198432fe8943934a9dc529ae99fc611d` |
| rootfs copies | inodes 250668646/250668647, identical hashes |
| load command | `@rpath/CydiaSubstrate.framework/CydiaSubstrate` |

## 4. Provider method and F1 runtime probes

Disposable thin probes were built from the frozen tree, signed with the
existing project entitlement profile, trustcache-admitted, and run through
`launchdchrootexec` in the chroot.

**Pre-install baseline (old provider):** `MSHookMessageEx` was a no-op:
`via-hook=42 replacement_called=0`, `VERDICT=PARTIAL`, provider UUID
`EC16F01C-5FE7-3D57-B0F1-3724BDC2E273`, old target SHA `1d3c525a…`.
The old WindowServer map (PID 50574) showed
`/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` and
`/usr/local/lib/libmachook_arm64.dylib`.

**Post-install probes:** both thin probes loaded
`/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` with
loaded UUID `A755DD35-D80C-3D90-9312-D566DE392770` and produced:

```text
fixture-before=42
original-published=<non-null>
original-direct=42
via-hook=43 replacement_called=1 original_called=1
VERDICT=INSTALLED-ORIGINAL-CALLABLE
exit 0
```

The repaired exact-class method hook therefore works against the installed
provider, and the published original IMP is callable. Both thin probes
loaded the **arm64e** provider slice; the distinct arm64 runtime slice was
not separately observed in these probes (the on-disk arm64 slice
`B9199E06…` is verified platform/minos/install-name and trusted).

**F1 route contract:** the device probe built from frozen
`include/macws_app_input_route.h` passed 9/9 cases:

```text
configure-window-flagged = RejectGlobalOwner
close-window-flagged     = RejectGlobalOwner
configure-window-zero    = RejectWindowRequired
global-pointer-zero      = GlobalSystemSurface
global-pointer-window    = GlobalSystemSurface
system-gesture-dock      = GlobalSystemSurface
system-gesture-nondock   = RejectGlobalOwner
relative-pointer-flagged = RejectGlobalOwner
relative-pointer-zero    = RejectWindowRequired
F1-VERDICT=PASS, exit 0
```

This is a device execution of the frozen pure contract, not a live
AppInputBridge/WindowServer production route. No production-safe trigger was
used, so a live F1 route witness remains missing; no input acceptance is
claimed.

## 5. Owned generation failure — load-bearing finding

The single bounded owned-generation restart failed:

```text
macos_gui.sh restart coexist --no-terminal --no-vnc  -> exit 1
minimal path: macOS system tccd protocol round-trip failed (status=4)
              TCCPING timeout service=com.apple.tccd.system after 5s
fallback:     systemstatusd did not start
              macos_gui.sh cleaned up and restored iOS
```

No WindowServer was created; the WindowServer loaded-provider map is
therefore missing. New crash reports at 18:26 show a common PAC fault:

```text
tccd-2026-10-18-182623.ips         pid 70481
authd-2026-10-18-182623.000.ips    pid 70469
systemstatusd-2026-10-18-182649.ips pid 70814

EXC_BAD_ACCESS / SIGSEGV
KERN_INVALID_ADDRESS at 0x2000000180ae1140 -> 0x0000000180ae1140
(possible pointer authentication failure)

_xpc_bootstrap_services +0xf140
loadImageCallback +0x6a4c
dyld4 add-image block ... InitStuff
```

The image is `/usr/local/lib/libmachook.dylib`, UUID
`76a98a5a-0c9c-32e5-93a3-b006f2bcd6d4` (the new installed libmachook). This
is the exact risk recorded in the R007 merged-provider audit: the LINKEDIT
fix now resolves the local `__xpc_bootstrap_services`, and the
unconditional call from `loadImageCallback`/`InitStuff` faults because the
PAC/ABI contract is not satisfied. It is a candidate regression, not a
provider/F1 failure. The failed restart's tccd/authd/systemstatusd are the
project/chroot services; native tccd 883, SpringBoard 872 and backboardd 905
stayed alive.

An install-time `ExcUserFault_codesign-2026-10-18-182444.ips`
(`EXC_GUARD`, namespc=7, reason=9) was also produced; `dpkg -i` still exited
0.

## 6. Separate acceptance endpoints

| Endpoint | Result |
|---|---|
| Package build/archive/provider contract | PASS (real `.deb`, 273 members, both provider arches/scripts) |
| Package install/provider publish/trust | PASS (dpkg 0; target fresh inode; both CDHashes live) |
| Loaded provider + method replacement/original | PASS in disposable chroot probes (loaded UUID `A755DD35…`, `INSTALLED-ORIGINAL-CALLABLE`); WS mapping missing |
| F1 route contract | PASS 9/9 contract execution; live production route missing |
| WindowServer process readiness | **FAIL** (tccd status=4; systemstatusd did not start; no WS) |
| Final-composite sequence/pixels | `state=missing producer=0 sequence=0`; no pixels |
| TCC reply | not re-tested; R008 empty-dictionary/attribution-failure baseline preserved |
| Accepted input | not re-tested; R008 postmin 1002 baseline preserved |

## 7. Cleanup and final state

- `macos_gui.sh stop` exit 0. The failed generation had already restored iOS.
- A leftover Host app PID 70969 (started by the failed generation) was
  TERM-stopped; no MacWSHost/WindowServer/displayd/inputd/interopd/agent/
  launchdchrootexec/probe process remains.
- Native SpringBoard 872 / native tccd 883 / backboardd 905 alive.
- Markers absent in both namespaces. No marker, cache or TCC policy/db edit.
- Package remains installed (`com.kdt.macosbooter 0.3.4`); published provider
  remains `7320a210…` with both CDHashes trustcache-live; no rollback was
  performed because none was requested, and the baseline provider/libmachook
  copies are preserved in
  `/var/mobile/macws_r010_evidence_20261018/baseline/` for a scoped
  follow-up decision.
- Probes preserved under
  `/var/mobile/macws_r010_evidence_20261018/probes/`.
- Thermal: `macos_gui.sh status` reported nominal, 34.59 °C; sysctl thermal
  remained unreadable.

## 8. Recommendation and limits

1. Do not accept this merged package as a working generation. Narrowly gate
   the `libmachook` `_xpc_bootstrap_services` call by role/identity, or
   correct its function-pointer PAC/ABI contract, then rebuild and repeat
   the owned-generation provider map. This is a source change for a
   follow-up packet, not made here.
2. The provider package/publisher path and the repaired
   `MSHookMessageEx`/F1 contracts behave correctly in the standalone probes;
   the missing runtime witness is the WindowServer generation itself.
3. The publisher's target code-sign Identifier is the temp basename
   `.CydiaSubstrate.new-<pid>`, not `CydiaSubstrate`; fix the publisher and
   re-verify.
4. Keep pixels, TCC reply and input as separate endpoints; R008's baseline
   remains unchanged.
5. Limits: host cross-build because device Theos is absent; both probes
   selected the arm64e provider slice; F1 is not a live production route; no
   WS/pixels/TCC/input; thermal sysctl unreadable.

Evidence: `coordination/artifacts/R010-provider-f1-package-20261018.json`,
ignored `analysis/r010-provider-f1/` (build/package/install/probe logs and
the tccd/authd/systemstatusd/codesign crash reports), and this file.

(handover to codex now)
