# Round 007 — frontend result: merged provider activation audit

Date: 2026-10-18. Read-only source audit; no device, source/test/build change,
build, package, Git write or background task. Only this file and
`docs/evidence/r007-merged-provider-activation-20261018.md` were added.

Candidate HEAD: `7e5d771`. Backend R004 device/native controls are untouched.

## Key identities

- `libmachook/mac_hooks.m` `e8ae9dad…eb08d`; `Metal_hooks.x` `a49c52c1…91e4a`;
  `MacWSSteamProcess.m` `4eae959f…a9d30`; provider source
  `misc/cydiasubstrate_macos.m` `6ac796c7…7c2b`; F1 header
  `include/macws_app_input_route.h` `29d7e17d…6440d`.
- Exact 22F82 SkyLight `analysis/SkyLight_22F82` `13d7a0ca…6c61`
  (UUID `96676A53-…5380`); local libxpc 15.6 `4de7869b…c9f8`.

## What the merge newly activates

**MSHookMessageEx (7 callsites) — FACT.** Provider `564-582` now installs
via `class_replaceMethod`; all 7 replacements forward to the original:

- Production-role compatibility: `pkd` → `-[LSPlugInKitProxy entitlements]`
  (`mac_hooks.m:15976`, gate `15987-15990`; restores real signed DER when the
  original is empty) and `MACWS_LSD_ROLE=session` →
  `_LSDefaults databaseStoreFileURL` / `...WithUID:` (`16161/16168`, gate
  `16148-16175`; re-homes the store URL, calls original).
- Diagnostic-only: MetalFX `_MFXTemporalScalingEffectV3
  encodeToCommandBuffer:` (Stray + `MACWS_STRAY_AGX_COMPAT` or sentinel
  `/tmp/macws_pipeline_diag`), AGX `newBufferWithLength:options:` /
  `didModifyRange:` (sentinel `/tmp/macws_iogpu_error_diag`), IOGPU primary
  buffer init (`MACWS_BUFFER_SUBALLOC_DIAG`). Production preflight's
  diagnostic pattern (`macws_diagnostic_flags.sh:105`,
  `macos_gui.sh:3768-3904`) must keep these off.
- Constructor timing: provider loads before libmachook initializers; PKD
  retries via add-image; LSD uses `dispatch_once` and can miss if
  `_LSDefaults` is not ready — verify in role.

**LINKEDIT local-symbol repair — FACT for exact 22F82 SkyLight.** All nine
targets are `t` local symbols: `MetalIOSurfaceBacking::PrepareForUse`
(0x31ffc4), `MetalContext::StartCompositeForDisplayStream` (0x148d74),
`StartComposite(WSCD)` (0x148358), `StartComposite(MTLTexture)` (0x148fb8),
`EndCurrentComposite(bool)` (0x14753c), `EndUpdateEb` (0x1470b0),
`deque<RenderState>::pop_back` (0x147fa0),
`WSCompositeDestinationCreateWithMetalTexture` (0x14b53c),
`StopCapture` (0x1471bc).

- **No function hook installs:** the permit table has exactly one
  registration (CGX sender, `mac_hooks.m:6040-6088`); every SkyLight target
  hits `REFUSED no permit` (`provider 433-459`). Pre-fix logs already show
  `REFUSED` while printing “installed”.
- **New direct-use activation:** `__xpc_bootstrap_services`
  (`mac_hooks.m:6360`) — local in 15.6 libxpc (`t` 0x18017bb84), pre-fix log
  `XPC_BOOTSTRAP: SYMBOL NOT FOUND`; post-fix may call it in every libxpc
  process. Exact 22F82 libxpc not local → verify before accepting.
- Steam private direct calls `_libAccessibilityBaseImplementationsLibraryCore`
  (`MacWSSteamProcess.m:1449`) and `_CreateSimpleProcess` (`1823`) may newly
  activate (private symbols; exact HIServices/libtier0 not local). Metal
  `_getMetalPluginClassForService` `%init` (`Metal_hooks.x:25099`) may newly
  activate the AGX plugin redirect if local; exact Metal not local — high
  impact, verify first.
- **Env-gated direct patch:** `WSCompositeDestinationCreateWithMetalTexture`
  now anchors the `MACWS_KEEP_RENDER_UPDATE_CBZ` diagnostic
  (`mac_hooks.m:6150-6190`); production preflight rejects the variable, so
  diagnostic-only.

## Unsafe activation / missing gates (propose only)

1. SkyLight `PrepareForUse` symbol-first selects `MetalIOSurfaceBacking`
   (0x31ffc4), but 22F82’s failing class is `MetalTiledBacking` (0x3243ac);
   the fallback is skipped. No permit today, but any future permit must target
   the right symbol.
2. “installed” logs after `MSHookFunction` refusal are misleading; callsites
   should verify `*result`/target bytes before logging.
3. `__xpc_bootstrap_services` has no role/env gate; newly active in every
   libxpc process. Verify result and bound it if needed.
4. Unconditional `macws_install_iohid_unserialize_bypass()` (`11986`) would
   no-op a public HID API if permits were broadened; keep unpermitted or gate
   it behind `MACWS_HID_BYPASS`.
5. Assert/HID/AGC bypasses remain env-only and unpermitted. Do not add
   permits; no blanket permit expansion.

## Packaging / loading path (both thin slices)

- Deployed provider image: `/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`
  (pre-fix probe log). Local host build LC_LOAD_DYLIB is
  `/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` — verify the
  deployed libmachook load command and chroot path per arm64/arm64e slice.
- Provider source is not in the Theos SUBPROJECTS; the next build must build
  both slices, run `set_macos_version.py` (macOS platform), sign/trustcache,
  install at the load path with fresh inode. Constructor order is safe
  (provider is a direct dylib dependency; no provider constructor).

## Minimal ordered validation plan

1. Freeze `3c4d581 + 4bcf20d + R008`; build once (libmachook + provider, both
   thin slices); record source/build/deployed SHA/UUID/load commands/CDHash.
2. Disposable provider controls on both slices: permit positive/negative,
   original callable, unpermitted target bytes unchanged; local method and
   local-symbol fixtures; `MSHookMessageEx` replacement + superclass intact.
3. Actual roles: WS (REFUSED/unpatched SkyLight; `XPC_BOOTSTRAP` result), pkd
   (entitlements adapter + original), lsd session (store URL + original),
   Steam (AX/CreateSimpleProcess), Metal/AGX (exact symbol locality, device
   class).
4. Cold start/recovery; real final-composite new pixels; Terminal
   click/text/arrows; Dock/menu/context; title-bar drag/menu tracking.
5. Rollback boundary (previous provider/libmachook/package, fresh inode,
   trust), cleanup, separate readiness/TCC/pixels/input reporting.

## Commands / exits

`grep` counts: 7 `MSHookMessageEx`, 26 `MSFindSymbol`, 1 permit registration.
`nm -a analysis/SkyLight_22F82` matched nine `t` symbols; `nm -a` on 15.6
libxpc matched local `__xpc_bootstrap_services`; local artifact marker
extraction and `sha256sum` exited 0; `git log` read-only exited 0. No
test/build/package/device command; no commit/reset/stash/push.

Full detail: `docs/evidence/r007-merged-provider-activation-20261018.md`.

(handover to codex now)
