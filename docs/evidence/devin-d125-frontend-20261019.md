# D125 — installed-vs-pending UI/F1 patch inventory (read-only audit)

Date 2026-10-19. Packet `coordination/_relay/devin-125-frontend-task.md`.
**No device commands, no edits, no builds, no Git/runtime changes.** All
device facts below come from artifacts already captured read-only
(`coordination/artifacts/current-ipad-patch-*-20261019*.json`, R008/R010/
R012/R013 evidence packets). All local facts come from `git`, the on-disk
`.deb` extracted to a temp dir, and `.theos/obj` timestamps/hashes.

## 1. Anchor: what is actually installed (FACT, device sha256, Oct-19 capture)

`dpkg -l`: `com.kdt.macosbooter 0.3.4` `install ok installed`. Last proven
package install is the **R012-era dpkg install** (`R012-xpc-pointer-fix-
20261018.json`: `remote_deb_sha256 c0b9fb22…`, `dpkg_exit 0`, mtimes on
device Oct 18 19:10–19:30). R008's recorded slices (`9a511953`/`151a2175`)
were superseded.

| Device path | sha256 | Identity | Provenance |
|---|---|---|---|
| `/var/jb/usr/macOS/lib/libmachook.dylib` | `47f78880…` | thin arm64e, CDHash `b579715e…` | R012 package (c97e219 + R012 xpc patch) |
| `/var/jb/usr/macOS/lib/libmachook_arm64.dylib` | `6de9b688…` | thin arm64 | R012 package |
| `/var/mnt/rootfs/usr/local/lib/libmachook.dylib` | `aa041efd…` | thin arm64e, Oct 18 23:53 | **out-of-package** D035 narrow-permit scp deploy (round-012-backend-result deploy step) |
| `/var/mnt/rootfs/usr/local/lib/libmachook_arm64.dylib` | `21f81b5c…` | thin arm64 | same |
| `/var/jb/…/CydiaSubstrate.framework/CydiaSubstrate` | `992b70d3…` fat arm64+arm64e; `.arm64 2ad4c652…`, `.arm64e 6dfa858e…` | ldid `CydiaSubstrate.47ba6b93.unsigned`, CDHash `45820e5b…`/`cc187cf8…` | R012 package payload, re-signed by postinst |
| `/var/mnt/rootfs/System/…/CydiaSubstrate` | `b2ae21da…` (R013 record, inode 250671913) | fat | postinst-published V2 provider |
| `/var/jb/Applications/MacWSHost.app/MacWSHost` | `d4a0ec51…` | thin arm64, `MacWSHost.b703f318.unsigned`, CDHash `d3011848…` | R012 package |
| `/var/jb/usr/macOS/bin/macos_gui.sh` | cksum `2532344555` 294789 | = local deb + R008 `471fc752` content | unchanged since R008 |
| `tccd` (rootfs) | `42c388e8…` (R008 record) | CDHash `1a32fd7e…`, 124 keys, PostEvent/ListenEvent/SysPolicyAllFiles | R008 acceptance, persists |

Package-vs-rootfs divergence is real: the **runtime** libmachook
(`aa041efd`/`21f81b5c`) is NOT the package's copy — it is the out-of-package
D035 V2 deploy. A package reinstall runs postinst and overwrites rootfs
copies with package slices; that reversion is expected, not corruption.

## 2. The local `.deb` is a STALE intermediate — not the installed package

`packages/com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb` =
`8601d8ff…`, 6027768 B, archived Oct 18 14:26. Its payloads are
byte-identical to `.theos/obj` products stamped Oct 18 13:09–14:19:
`MacWSHost 28e85425` (13:09), fat `libmachook 5b7e3b2d` (14:19),
daemons 13:13–13:14 (`obj==deb` verified for inputd/interopd/displayd/
launchservicesd/workspacectl/tccdprobe).

Decisive ordering: `4bcf20d` (F1 fix) committed **15:51**, `b717fb9`
(provider packaging) **17:39** — both AFTER the last payload build 14:19.
Therefore, by timestamp alone (no inference from source state needed):

- the deb's libmachook **cannot contain the F1 fix**;
- the deb contains **no `CydiaSubstrate.framework` payload** (verified by
  extraction — absent entirely);
- its `DEBIAN/postinst` = `c45c1d36` = the R008 postinst, not the b717fb9
  provider-publish version;
- its `macos_gui.sh` `471fc752`/`postinst.sh` `ed42f478` do match the
  R008-era records (those files are simply unchanged since).

**Consequence for D124: do NOT install this deb.** It would revert the
installed F1 fix and the provider payload. It resolves the D124 task's
"relationship UNKNOWN" question: stale pre-F1/pre-provider intermediate.

## 3. Inventory classification

| Change | State | Evidence |
|---|---|---|
| `3c4d581` provider method hooks + AppInput route contract | **installed** (R012 package + rootfs V2 build) | R010 freeze `AppInputBridge e794623b`, `mac_hooks e8ae9dad` (=committed HEAD); R010 on-device disposable-chroot probe: provider identity + MSHookMessageEx/F1 contracts PASS |
| `4bcf20d` F1 exact-window rejection ordering | **installed, never exercised end-to-end** | header `29d7e17d` in R010 freeze and committed; R010 probe passed F1 contract; R013 generation: input still `1002`, no exact-window witness |
| `b717fb9`+`c97e219` provider package payload contract | **installed** | framework in `/var/jb` package + rootfs publish `b2ae21da`; postinst publish path exercised at R012 install |
| R008 lifecycle/readiness/trust package | **installed, verified, then superseded** | `8e470ba`+acceptance JSON; libmachook slices since replaced; tccd entitlements persist |
| R012 xpc-pointer patch (14+/1-, uncommitted) — sign raw `MSFindSymbol` result `ptrauth_key_asia` before `blraaz` in `_xpc_bootstrap_services` call | **installed** (inside R012 package libmachook) **and folded into dirty source** | `R012-xpc-pointer-fix-20261018.json`; current dirty `mac_hooks.m` diff hunks @@1728/1774 carry it inline |
| D035 narrow-permit V2 libmachook (`aa041efd`/`21f81b5c`) | **installed separately** (scp to rootfs only, not package) | round-012-backend-result deploy step; devin-033/041 identify it as repo `.theos/obj` code |
| D043–D048 sender-observation integration | **source-only, uncommitted, unbuilt** | dirty `mac_hooks.m 7321d92c` + `Makefile` + untracked `MacWSSenderObservation.{c,h}` (`6641d9df`/`a7c8438b` = D048-frozen); zero `SENDEROBS` strings in installed dylib (device capture); local obj/deb both predate it |
| `misc/cydiasubstrate_macos.m` relocation/diagnostic changes | **source-only, uncommitted** | dirty `fa6579a2` vs committed `6ac796c7` (R012 canonical); mtime Oct 18 23:34 — post-dates the installed provider build (~19:xx) |
| macwshostd / macwsinputd / interopd / displayd / control_probe / launchservicesd / workspacectl | **installed** (R012 package) | device cksums differ from local deb only by build/signing; no source delta in window |
| MacWSHost.app | **installed** `d4a0ec51` thin arm64 | R012 package; no MacWSHost source delta since; thin-arm64 is the artifact's only arch in both packages (contract-required payload, arm64e slice never shipped) |
| `misc/macos_ws_event_probe` (untracked **compiled binary**), `misc/macws_sender_obs_probe.c`, `misc/test_mshook_relocate*.c`, `misc/test_sender_observation.c`, `tmpdir-lc/` | **source-only/untracked, exclude from package** | not in deb (verified); the compiled probe is a stray Mach-O in the source tree — ensure staging cannot sweep it in |
| D113/D120 bounded diagnostic transport (`analysis/d113-trace-transport/`) | **documentation/offline-only — correctly absent from product** | not referenced by `libmachook/Makefile`, not in deb payload; test-only per its own contract |
| all `docs/evidence/*`, `coordination/*`, DevinWatchdog files | **documentation/offline-only** | relay/review/watchdog state |

## 4. R003 relay gate + F1 vs installed artifacts and arch identities

Route decision layers (source-verified):

1. **Producer**: `MacWSHost` (`main.m` emits `MacWSInputRecord` incl.
   `ConfigureWindow`) and `macwsinputd` broker.
2. **Relay gate** (`macwsinputd/main.c`): control-plane kinds
   (`ConfigureWindow`/`CloseWindow`/`PerformQuit`) always go
   `SendToAppInputBridge` (l.1689-1700); pointer/global kinds take the
   routed path where `routedRecord.flags |= MacWSInputFlagGlobalSystemSurface`
   can be stamped on a resolved Dock system surface (l.2203) — **so the
   broker can emit `ConfigureWindow`-class records carrying the global
   flag in adjacent paths; F1 is load-bearing, not merely defensive.**
3. **Consumer gate** (`include/macws_app_input_route.h` →
   `libmachook/AppInputBridge.m`): post-F1 ordering rejects
   global-flag-on-exact-kind with `RejectGlobalOwner(-7)` before the
   generic global branch; pre-F1 it accepted `GlobalSystemSurface(2)`.

Against installed artifacts:

- **libmachook arm64e `47f78880` + arm64 `6de9b688`** — built from
  c97e219+R012-patch (post-F1 source); R010 evidence records the
  provider/F1 contracts passing in disposable chroot probes on device.
  Both thin slices verified by ldid/lipo in the Oct-19 capture.
- **rootfs `aa041efd`/`21f81b5c`** — later V2 build, also post-F1 source.
- **MacWSHost `d4a0ec51` thin arm64** — contains no route-classify code
  (header consumed only by libmachook); its producer role is unchanged
  by any pending commit (no MacWSHost source delta). Thin arm64 is the
  shipped identity in both the installed package and the stale deb —
  consistent, not a new gap.
- **Provider fat `992b70d3` + thin companions** — V2 permit-aware
  (`MSHookFunction` fail-closed without `(uuid,offset,prologue)` permit);
  ldid identifier still a hashed-suffix `…unsigned` (publisher temp-
  identifier finding from R010/R012 persists in effect).

Live posture (device captures): WS `state=missing`/GUI `exit1`; V2
libmachook permits are diag-marker-gated → zero SwapEnd hooks in
production → no composite publish. F1 is inert until a generation reaches
input delivery.

## 5. Focused tests + concrete on-device witness for exact-window input

Host-side (runnable now, no device):
- `python3 -m unittest misc.test_app_input_route_contract` — F1 matrix:
  ConfigureWindow/CloseWindow × window{0,nonzero} × flag{off,on} →
  `-7`/`-6`/`1`; RelativePointer/SystemGesture/global pointer kinds
  unchanged; source-order pin (l.270) prevents regression.
- `python3 -m unittest misc.test_cydiasubstrate_provider_contract
  misc.test_substrate_package_contract misc.test_artifact_contract` —
  provider/permit/payload contract.
- `misc/test_sender_observation.c` + `misc/test_mshook_relocate*.c` —
  obs and provider-relocation unit coverage (untracked, keep out of deb).

Concrete on-device witness (for D124's layer 6 — all must hold, in order):
1. A real generation where the broker routes a `ConfigureWindow` record
   with **nonzero CGWindowID in sceneID's high half, no global flag** to
   the target app's `/private/tmp/macws_app_input.<pid>.sock`;
   `MACWS-INPUT APP-CONTROL kind=configure-window … sent=YES` in inputd
   log AND AppInputBridge accepting `route=ExactWindow` with
   `windowWithWindowNumber:` resolving non-nil AND the window applying
   the configure (observable geometry change).
2. Negative control: same record with `MacWSInputFlagGlobalSystemSurface`
   stamped → rejected `-7` at classify; **no** AppKit window effect and
   no global-surface delivery.
3. Freshness via the **existing** reopen path only: the resolved window
   must belong to the current generation — `MacWSStreamClient`
   `reconnectGeneration` invalidation plus AppInputBridge's reopen
   postcondition (`main.m:10258-10338`) — do not add a second freshness
   mechanism; a witness on a stale-retained window does not count.
4. `SLSPostEventRecord`-class replies for the same window must show a
   real acceptance/return value other than the standing `1002` before
   input acceptance is claimed.

## 6. D124 manifest review — omissions and unsafe-mixing risks

- **Omission — R012 xpc-pointer patch is not named.** The dirty
  `mac_hooks.m` delta is not "D043–D048 only": hunks @@1728/1774 carry
  the uncommitted R012 fix that resolved the R010 PAC crash
  (`tccd/authd/systemstatusd` died at `_xpc_bootstrap_services`). A clean-
  worktree HEAD build regresses to the crashing libmachook; a dirty-tree
  build keeps it. The manifest must name it explicitly and treat it as
  load-bearing, not diagnostic.
- **Unsafe mixing — the stale `.deb`.** D124 asked the right question
  ("relationship UNKNOWN"); the answer is now FACT: it predates F1 and
  the provider payload. Installing it would revert both.
- **Divergence handling.** Package libmachook ≠ rootfs libmachook (D035
  out-of-package V2). A reinstall reverts rootfs to the new package's
  slices — correct only if the new package is a strict superset of the
  V2 narrow-permit build; D124 must diff its built slices against
  `aa041efd`/`21f81b5c`'s content, not just the package copies.
- **Provider identifier persists.** Published provider keeps a hashed-
  suffix `unsigned` ldid identifier (`47ba6b93`); the R010/R012 finding
  remains open — record actual post-install identifier/CDHash again.
- **Stray artifacts.** `misc/macos_ws_event_probe` is an untracked
  compiled Mach-O inside `misc/`; `tmpdir-lc/` is untracked. Verify
  staging excludes both (current deb does; keep it that way).
- **Sender-obs is default-off diagnostic.** Even after install, obs code
  is inert without its dedicated sentinel — D124's "one bounded record-
  only event" layer is the only intended activation. Keep it that way.
- **D113/D120 stays out** — verified absent from the deb and from
  `libmachook/Makefile`; it is a test prototype, never product payload.

## 7. Remaining gaps (no acceptance claimed)

- F1 fix: installed in both libmachook identities, probe-verified at
  contract level, but **no end-to-end exact-window witness exists** —
  input is still `1002` in the last real generation.
- Rootfs provider `b2ae21da` vs package `992b70d3` differ by publish/
  re-sign step; the publisher's temp-identifier issue is open.
- `macwsinputd`/interopd/displayd installed hashes were not captured in
  the Oct-19 fingerprint run (cksum only for some) — minor inventory gap.
- WS `state=missing`, GUI `exit 1` unattributed; no pixels, no composite,
  no accepted input. Candidate remains **PENDING**.

(handover to codex now)
