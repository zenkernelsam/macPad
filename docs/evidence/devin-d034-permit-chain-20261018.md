# D034 — production hook permits for the composite-publish chain

Date: 2026-10-18 (backend, continuation of D033)
Scope: source + build only. No device mutation, no generation start/stop,
no deployment. Authoritative rules read: AGENTS.md.

## Causal chain (D033 finding → this change)

D033 established the live watchdog generation is frameless because the
fail-closed provider (`misc/cydiasubstrate_macos.m`, R003-F02) refuses every
MSHookFunction target that lacks a registered (image-uuid, image-offset,
expected-prologue) permit — and the only registration site was gated behind
`macws_runtime_diagnostics_enabled()`, so production registered zero permits
and all 21 SkyLight/IOMFB hook installs in WS were refused.

Publish path dependency (verified in source this round):

```
MacwsIOMobileFramebufferSwapEnd_new            mac_hooks.m:27408+
  → macws_iomfb_complete_cancelled_swap        :21592
    → registration.callback (populated ONLY by the
      macws_enable_frame_info_tag_list hook)   :19640-19700
    → macws_publish_graphics_ready()           :21691
    → macws_publish_scanout_after_swap()       :21699
      → macws_latest_owned_scanout             Metal_hooks.x:8327
        (pool fed by swizzle2 texture hooks — NOT permit-gated)
```

So the permit-gated sites that are load-bearing for visible output:

| # | Site | Image | Permit material |
|---|------|-------|-----------------|
| 1 | IOMFB `publicSwapEnd` (+0x11cc) | IOMobileFramebuffer | ABI table `wrapperSig0-3` = RE prologue; uuid = live LC_UUID (ABI already pins build) |
| 2 | QC `enable_frame_info_tag_list` (+0x29285c) | QuartzCore | `kMacWSQCFrameInfoABIs` uuid CF853BBD… + `enablePrologue` |
| 3 | QC `begin_skylight_update` (+0x291288) | QuartzCore | same table `beginPrologue` |
| 4 | QC `finish_skylight_update` (+0x291220) | QuartzCore | same table `finishPrologue` |
| 5-12 | SkyLight composite set (sym1 PrepareForUse, sym2 StartCompositeForDisplayStream, sc_wscd, sc_mtltex, end_composite, end_update, WSCDCreateWithMetalTexture, StopCapture) | SkyLight 22F82 | uuid 96676A53… pinned; offset = resolved-sym − header; prologue recorded at resolution (fallback-resolved sites were already 12-word memcmp-verified; MSFindSymbol sites are vetted by the (uuid,offset) binding + symbol name) |

12 permits total; `MACWS_MAX_HOOK_PERMITS = 32` — fits with headroom
(the diag-only CGXSYNTH permit adds one more only when the marker exists).

NOT permit-gated (verified): `swizzle2` ObjC texture swizzles
(`method_exchangeImplementations` direct — feeds the owned-scanout pool) and
the SL-PIPE begin-update diag probes (`macws_qc_inline_hook` own trampoline).
Other env/process-gated MSHookFunction sites (IOHID bypass, AGX renamer,
assert/abort diagnostics, VNC-server internals, AESend, thumbnails,
desktop-volume/filecache diagnostics) still refuse — intentionally, until a
round proves them needed; their refusals are now named.

## Changes

`libmachook/mac_hooks.m`:
- New `macws_macho_image_uuid` (LC_UUID walker) +
  `macws_register_hook_permit` (dlsym-cached `MACWSHookPermitV2`; provider
  absent → -9 → fail closed, same as unregistered).
- New file-scope `kMacWSSkyLight22F82UUID` constant.
- New `macws_sl_hook_permitted` guard for the 8 SkyLight sites.
- Permits registered at: SwapEnd install, QC frame-info observer install,
  QC begin/finish pacing install. On refusal each site logs a named reason
  and skips the install (same failure semantics as before, now attributed).

`misc/cydiasubstrate_macos.m`:
- `MACWS_REFUSE` now emits `target=%p image=%s+%#llx sym=%s` via dladdr —
  turns every future refusal into a target enumeration line.
- `MSHookMessageEx` refusal now names class/selector/imp (the two current
  `MSG-REFUSED bad args` become identifiable).
- `#include <stdio.h>` added for snprintf.

## Built artifacts (theos, this host)

- `libmachook/.theos/obj/arm64/libmachook.dylib` sha256 `56c4bc76…0a76c`
- `libmachook/.theos/obj/arm64e/libmachook.dylib` sha256 `bc2b549c…a6700`
- `CydiaSubstrateProvider/.theos/obj/macosx/CydiaSubstrate` (fat) sha256 `694be5a2…d41d2d`
  - arm64 slice `22dfcb54…21d92`, arm64e slice `c37838b2…21a70`

Compile clean on both archs (only pre-existing `-undefined dynamic_lookup`
/`-multiply_defined` ld deprecations).

## Expected behavior on device (untested — next bounded packet)

- WS log gains `COEXIST API SwapEnd hook installed`, the QC observer install
  line, pacing install, and SkyLight `…hook installed` lines matching the
  Oct-12/13 `.bak` era; `REFUSED` lines that remain will be named.
- Witnesses for acceptance: `replay-receiver-ready`, `CANCEL-COMPLETION
  delivered` counters, `macws_final_composite.state` → `ready` with
  advancing `sequence` + nonzero `producer`, then visible wallpaper/Dock/menu
  bar in MacWSHost. Input remains a separate acceptance axis.

## Remaining gaps / honesty notes

- SkyLight sites record the prologue from the resolved address; their real
  build pin is uuid+offset (uuid hardcoded 22F82). QC/IOMFB sites carry full
  RE'd prologue pins. A wrong-build SkyLight still fails closed on uuid.
- Sites in non-WS processes (VNC server, AESend, thumbnails, IOHID, AGX
  diagnostics) remain unpermitted by design this round.
- `MSG-REFUSED bad args` ×2 (ObjC swizzle path) is a separate defect —
  class/imp NULL at two call sites; now named by the provider log.
- Deploy requires BOTH new libmachook (permits) — provider change is
  diagnostic-only and may ship alongside, or the A/B may deploy libmachook
  alone for the strictest one-variable test.
