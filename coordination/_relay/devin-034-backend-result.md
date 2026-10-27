# Devin backend D034 — composite-chain hook permits implemented + built

Continuation after D033 handover (user: "Please continue and non-stop").
No new task file existed, so I implemented the D033-recommended next action:
register production MACWSHookPermitV2 permits for the composite-publish
chain the fail-closed provider was refusing.

## What changed (source only — no device touch)

- `libmachook/mac_hooks.m`
  - `macws_register_hook_permit` + `macws_macho_image_uuid` helpers;
    `kMacWSSkyLight22F82UUID` constant; `macws_sl_hook_permitted` guard.
  - Permits now registered for all 12 WS load-bearing MSHookFunction sites:
    IOMFB `publicSwapEnd`, QC `enable_frame_info_tag_list`, QC
    begin/finish pacing, and the 8-site SkyLight composite block.
  - Refusal at any site logs a named rc and skips install (fail closed).
- `misc/cydiasubstrate_macos.m`
  - REFUSED log now names `image+offset` + symbol via dladdr.
  - MSG-REFUSED now names class/selector/imp.
  - No gate weakening; the contract is unchanged.

## Build status

- libmachook arm64 `56c4bc76…0a76c`, arm64e `bc2b549c…a6700` — clean.
- provider fat `694be5a2…d41d2d` (arm64 `22dfcb54`, arm64e `c37838b2`) — clean.
- Evidence doc: `docs/evidence/devin-d034-permit-chain-20261018.md`.

## Recommended next (needs its own authorized packet)

One-variable owned-generation A/B:
1. Hash-preserve current installed libmachook slices + provider.
2. Deploy new libmachook (+ provider for named-refusal logging).
3. Start one bounded generation; capture WS stderr.
4. Accept only on: `SwapEnd hook installed` + `replay-receiver-ready` +
   `CANCEL-COMPLETION delivered` counters + `macws_final_composite.state`
   `ready` with advancing sequence + visible wallpaper/Dock/menu bar
   witness in Host.
5. Input remains separately gated; do not roll it into display acceptance.
6. Any remaining `REFUSED`/`MSG-REFUSED` lines now name their targets —
   feed them back into the permit table instead of bypassing.

## Limits

- Not yet device-validated; liveness of the fix is unproven.
- Non-WS processes' hook sites remain unpermitted (documented).
- The two `MSG-REFUSED bad args` swizzle failures are a distinct defect,
  now identifiable in logs.
- R012 pointer-signing edit is still in the working tree alongside this
  change; nothing committed this round.

(handover to codex now)
