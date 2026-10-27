# devin-198-backend-result

Status: **complete — read-only inventory**. No edits, builds, device,
or integration. D190 candidate untouched (freeze honored). Evidence:
`docs/evidence/devin-d198-backend-20261020.md`.

## Inventory vs the six D140 edges

All six offsets (`0x33C63C`/`0x338E48`/`0x1064A8`/`0x1064E0`/
`0x33C3A4`/`0x33C4FC`) are **absent from libmachook** — zero permit
registrations, zero hooks. D140's "no existing permit" still holds.

Existing permits (provider `misc/cydiasubstrate_macos.m` — runtime-
populated whitelist, fail-closed `MACWSHookPermitV2`): SkyLight
`0x318464` gate + SENDEROBS `0x3407ec`/`0x340da0` (isWS + sentinels),
plus IOMFB/QuartzCore ABI-pinned sites. SENDEROBS impl `0x340da0` ≠
D140 `_CGXHandleMessage` `0x1064E0` — no overlap.

**Present**: mach_msg/mach_msg_overwrite/mach_msg2 interposes
(:15437-39) — the recv/send proxies — but they emit only
`MACWS_MACH_MSG_ALL` stderr diagnostics. `macws_macho_uuid_matches`
compare helper.

**Missing**: all 6 permits+hooks, services-tid recording, gen/UUID
production metadata (candidate-only), drain consumer + serializer,
`_current_message` `0x1d8ba5b88` read, D8 exclusivity enforcement
(umbrella trace exists; nothing prevents co-arming), dedicated arming
sentinel.

## Smallest reviewable milestone

**WS services-pass observer** — one edge pair, PASS_ENTER/EXIT at
`0x33C63C`, + D141 ring emit. Exercises permit→hook→record→ring→drain
→stream end-to-end with no msgid join; D169's PASS-density tid rule
already fits. Gate: UUID-pinned permit only; sentinel off ⇒ zero
records, on ⇒ ≥30 monotonic PASS spans across ≥2 cold starts;
MACH_MSG_ALL unset (D8); drops=0 idle; visible composite unaffected.

(handover to codex now)
