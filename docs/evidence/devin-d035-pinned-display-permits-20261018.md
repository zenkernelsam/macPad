# D035 — narrow independently-pinned display permits

Date: 2026-10-18. Scope: local source+build only. No device access, no
deploy/start/stop, no commit/package. Per reviewer correction, all dynamic
(live-read/derived) permit values were removed; every registered permit is
pinned by independently recorded uuid + image offset + RE'd 16-byte prologue.

## Correction vs D034 (which was not accepted)

- Removed `macws_sl_hook_permitted` and all 8 SkyLight permit gates; the
  SkyLight composite wrappers revert to pre-D034 form (attempt MSHookFunction,
  provider fails closed unpermitted). Deferred, not deleted.
- Removed `macws_macho_image_uuid` and the NULL-uuid live-pin path;
  `macws_register_hook_permit` now requires a non-NULL pinned uuid.
- Remaining permit set = the proven publisher chain only:
  - QC `IOMFBServer::enable_frame_info_tag_list` (captures frame-info
    registration feeding `macws_iomfb_complete_cancelled_swap`)
  - QC `begin_skylight_update` / `finish_skylight_update` (pre-lock pacing)
  - IOMFB `publicSwapEnd` (drives completion → `macws_publish_scanout_after_swap`)
- 4 permits; `MACWS_MAX_HOOK_PERMITS`=32 unaffected.

## Independent pins (all from existing RE'd tables)

| Target | Image | Offset | Prologue | UUID |
|---|---|---|---|---|
| enable_tag_list | QuartzCore 13.4 | 0x29285c | `d503237f d10243ff a9036ffc a90467fa` | CF853BBD-01B6-3F46-ADA1-EC70FD2DC9DC |
| begin_update | QuartzCore 13.4 | 0x291288 | `d503237f a9be4ff4 a9017bfd 910043fd` | same |
| finish_update | QuartzCore 13.4 | 0x291220 | `d503237f a9be4ff4 a9017bfd 910043fd` | same |
| publicSwapEnd | IOMFB 13.4 | 0x11cc | `b4000080 f9439401 b4000041 d61f083f` | 7AC531B7-05F5-38FE-92E5-DA6D86D22A14 |

IOMFB uuid provenance: `otool -l` LC_UUID on the device-extracted image
`/var/mnt/rootfs/private/tmp/dscout_ventura/.../IOMobileFramebuffer`
(captured during D033's read-only inventory). RISK: that extraction's
cpusubtype reads arm64e; if the live WS loads the arm64 slice the uuid pin
mismatches → permit `-2` → hook skipped → still fail-closed and now
named/logged. This is the single remaining relocation risk.
15.6.1 IOMFB row has uuid `{0}` → registration always -2 → fail closed.

## Kept diagnostics (bounded, none claim proof)

- Provider REFUSED lines now name `image+offset`/`sym` via dladdr;
  MSG-REFUSED names class/selector. Telemetry only.
- Permit-registration failures log `rc` and skip install.

## Builds (theos, this host)

- libmachook arm64 `bf3ae467…dc30`, arm64e `f2143308…9146`
- provider fat `694be5a2…d41d2d` (unchanged vs D034 build — logging delta
  only, contract untouched)
- `git diff --check` clean. R012 PAC hunk and other user work untouched.

## Not run

- No device test, no live contract test this round (macosx-target provider
  is buildable for a host-side dlopen harness — suggested next). Missing
  inputs needed for full pinning confidence: live IOMFB arm64-slice LC_UUID
  from the running WS (one bounded read), and whether the SkyLight tolerate-
  nil wrappers are also required for pixel acceptance (decidable only after
  the 4-permit generation runs — the A/B ladder answers it).
