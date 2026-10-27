# D082 frontend result — line-precise patch plan (no application)

Offline source review only; nothing edited, built, deployed, or sent.
Evidence: `docs/evidence/devin-d082-frontend-20261019.md`.

## Patch plan (≈7 lines)

- **Helper** after `mac_hooks.m:9076` (sibling of
  `macws_keyboard_latency_diagnostics_enabled`):
  `macws_frame_pace_diagnostics_enabled()` =
  `access("/tmp/macws_frame_pace_diagnostics", F_OK) == 0` — uncached
  per call, fail-closed.
- **Site 1** `mac_hooks.m:20056` (pre-lock pace emit,
  `interval=`/`slept=`, already bounded `<=4 || %600`): OR the new
  helper into the umbrella gate.
- **Site 2** `mac_hooks.m:27395` (activity-pace emit, mode/age fields,
  bounded to mode transitions): same OR.
- OR-ing preserves all existing umbrella behavior; both sites already
  throttle their own output — no extra throttling needed.

## Contract

Uncached access = live arm/disarm between frames (matches the
D041-proven dedicated-sentinel precedent); chroot `/tmp` = host
`/var/mnt/rootfs/tmp`; both namespaces swept on disarm (prior
landmine). Produces exactly the "artificial wait vs message
availability" measurement with zero display-producer confound.

## Why current generation is unmeasurable (FACT)

Umbrella helper is cached-once (latched at first call — arming now
cannot take effect); the umbrella also activates the alternate display
producer (non-causal by contract); the helper+gates live inside the
deployed dylib — no filesystem toggle can inject code into the running
process.

## Activation authorization required (not granted)

rebuild + redeploy (signing/trustcache re-admission) + **WS restart**
under the session/rollback protocol (hooks/pacing install only at
launch) + sentinel-lifecycle signoff (chroot filesystem mutation,
D041-precedented). Until then: prepared, not measurable.

(handover to codex now)
