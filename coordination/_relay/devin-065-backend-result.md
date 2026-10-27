# D065 backend result — TCC endpoint route map + role contract (read-only)

2026-10-19. Read-only job/plist/log/status reads + source audit. No TCC, no
endpoint switch, no service ops, no deploy, no restart.
Evidence: `docs/evidence/devin-d065-endpoint-contract-20261019.md`.

## Route map (closed)

- **WS → Ventura system tccd**: WS is uid 0 → TCC `tccd()` picks
  `com.apple.tccd.system` → libmachook maps →
  `com.apple.macosbooter.tccd.system` (+ drops privileged flag 0x2) →
  launchd-vendored endpoint of job `com.macwsguide.tccd-system` (pid 10300,
  `user/501`, `tccd system` → isSystemServer=1, UUID 9845D17F).
- **Server check-in is mapped identically**: tccd's own
  `create_listener("com.apple.tccd.system")` → remapped → checks in the
  macosbooter name (endpoint `active=1`, port 0xc840f). Round-trip
  consistent — server and client translate through the same table.
- **`com.apple.tccd` unmapped** → iOS native user tccd pid 883 (UUID
  0120CE08, different protocol generation). `com.apple.tccd.system` is
  never registered on iOS. **No Ventura user-tccd job exists.**
- Interpose audit: `__mac_syscall`/`__sandbox_ms` shims are AMFI-only and
  env-gated — the `Quarantine`/`0xb6` path forwards verbatim. The
  responsibility query is not shimmed.
- `tccd-system.ready` holds a prior `TCCPING` → `count=0` empty reply —
  probe traffic, not WS-originated. **No WS-originated TCC request has
  ever been witnessed**; SkyLight's audit-token preflight callsite is
  RE-verified but its invocation in the current generation is unobserved
  (AppInputBridge input path never crosses TCC).

## Role verdict — user path is not a legitimate alternative

PostEvent/ListenEvent/ScreenCapture are compile-time `macos_isPerSystem=1`
(frontend registry map). A non-system instance hits the `0x6710` early-bail
→ identical empty reply, handler never runs. The role contract itself — not
just attribution — makes input/display services system-only. Global-input
synthesis is therefore unreachable by construction on this deployment,
independent of the provider hypothesis.

## Next experiment

Unchanged smallest authorized step: `__mac_syscall` A/B on the live kernel
(`"Quarantine"` vs `"Sandbox"`, one syscall each, zero TCC) — resolves the
provider hypothesis at runtime. Nothing else needed for routing.

Desktop untouched (`ready` seq 5145 last read). No state-affecting calls.

(handover to codex now)
