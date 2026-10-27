# D198 — WS observer/permit inventory (read-only)

Task: `coordination/_relay/devin-198-backend-task.md`. Reconcile
D140's six WS service-entry edges against current `libmachook`
source and the `MACWSHookPermitV2` provider; inventory present vs
missing across permits, hooks, services-tid recording, gen/UUID
metadata, drain/serializer, D8 exclusivity. **Source ≠ live
installation** — this audits the working tree only. D190 candidate
untouched (frontend review freeze honored).

## The six D140 edges (UUID 96676A53-…-B73873CD1880)

| edge | function | img offset | permit? | hook? |
|---|---|---|---|---|
| PASS_ENTER/EXIT | `CGXRunOneServicesPass` | `0x33C63C` | none | none |
| DISPATCH_BEGIN | `_post_port_data` | `0x338E48` | none | none |
| HANDLER_BEGIN | `rendezvousHandler` | `0x1064A8` | none | none |
| IMPL_INVOKE | `_CGXHandleMessage` | `0x1064E0` | none | none |
| REPLY_ENQUEUE | `CGXPostReplyMessage` | `0x33C3A4` | none | none |
| FLUSH_BEGIN | `_push_out_message` | `0x33C4FC` | none | none |

No occurrence of any offset exists in `libmachook/` (grep: zero hits).

## Existing permit registrations (current tree)

Provider: `misc/cydiasubstrate_macos.m` — `g_permits[]` is a
*runtime-populated* whitelist, not a static table.
`MACWSHookPermitV2` (:371) verifies live image UUID (−2) + 16-byte
expected prologue (−1), then appends; the real `MSHookFunction` site
(:478-486) refuses any unpermitted target (`MACWS_REFUSE`). So "permit
table" = the call sites:

| site | image | offset | gate |
|---|---|---|---|
| `mac_hooks.m:6161` | SkyLight-22F82 | `0x318464` CGXSenderCanSynthesizeEvents gate | isWS + diag **or** senderObs |
| `mac_hooks.m:6192` | SkyLight-22F82 | `0x3407ec` dispatcher audit-right | isWS + senderObs |
| `mac_hooks.m:6195` | SkyLight-22F82 | `0x340da0` impl tcc-check | isWS + senderObs |
| `mac_hooks.m:6425` | IOMFB | `publicSwapEndOffset` | iomfbAbi match |
| `mac_hooks.m:19992` | QuartzCore | `enableTagListOffset` | QC ABI match |
| `mac_hooks.m:20127/129` | QuartzCore | begin/finishSkylightUpdate | QC ABI match |

SkyLight-pinned registration helper:
`macws_register_hook_permit` (:1786) — dlsym capability lookup
(missing provider → −9, hook stays uninstalled; fail-closed).
Constants in `MacWSSenderObservation.h:108-117` +
`.c:25-31` (byte-verified prologues).

**Note**: SENDEROBS `0x340da0` is the TCC impl observer — NOT the D140
`_CGXHandleMessage` edge (`0x1064E0`). No overlap.

## Inventory verdicts

| capability | status | evidence |
|---|---|---|
| 6 permits for the edges | **MISSING — all 6** | offsets absent from tree |
| 6 hook installs | **MISSING** | no MSHookFunction targets exist |
| recv/send legs | **proxies INSTALLED** | `DYLD_INTERPOSE` mach_msg/mach_msg_overwrite/mach_msg2_internal (:15437-15439) — currently emit only `MACWS_MACH_MSG_ALL` stderr diagnostics |
| impl-invoke proxy | **missing read** | `_current_message` `0x1d8ba5b88` not referenced |
| services-tid recording | **MISSING** | no emit/ring code anywhere in libmachook |
| gen/UUID record metadata | **MISSING in production** | `d141_set_generation`/`d141_get_uuid` exist only in `analysis/d141-svctime-candidate/` (committed `cd3bc6d`, analysis-only); libmachook has only `macws_macho_uuid_matches` (compare helper, no retention) |
| drain consumer + serializer | **MISSING** | `d141_drain`/lease + D169 v3 writer are candidate-only; no consumer thread, no stream file writer |
| D8 exclusivity w/ `MACWS_MACH_MSG_ALL` | **MISSING enforcement** | umbrella trace exists (env-gated :13008, used :13227/:13501/:13564); nothing prevents co-arming once svc arming exists — contract says enforcement belongs to the integration layer |
| dedicated arming sentinel | **MISSING** | senderObs sentinel pattern exists (`macws_sender_observation_enabled`) as the model to copy |

## Smallest reviewable diagnostic-only observer milestone

**"WS services-pass observer"**: ONE edge pair —
`CGXRunOneServicesPass` ENTER/EXIT (`0x33C63C`) — plus the accepted
D141 ring, emitting PASS records only. This is the minimal slice that
exercises the whole integration stack (permit → hook → record → ring
→ drain → stream) without needing the msgid-correlation join; the
services tid is identified by PASS_ENTER density exactly as the
accepted D169 decoder already does.

Runtime acceptance gate (all required, independently witnessed):
1. Permit granted on UUID-pinned live image only; prologue bytes
   verified by provider at install; wrong-build image → refusal.
2. Sentinel off → zero PASS records; sentinel on → ≥30 PASS spans
   across ≥2 cold starts, monotonic `ts_ns`, consistent services tid.
3. `MACWS_MACH_MSG_ALL` unset for the armed window (D8); drops=0 at
   idle rates; ring bounded under stall.
4. No return-value/registers touched, no locks across waits; WS
   composites normally (visible-desktop witness still required).

Explicitly out of scope: the other five edges, impl-invoke global
read, drain transport beyond a test file, and any correlation claims.
