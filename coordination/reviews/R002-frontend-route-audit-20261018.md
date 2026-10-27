# Qoder frontend R002 route audit — CHANGES_REQUESTED

Worker: qoder `macPad移植-前端`, PID 38533, `/dev/ttys000`, session `02A55FDA-3ED9-47B0-AD3A-DCF4E26FD263`. Review was handed over with a read-only report; no device or package command was run.

## Finding F1 — global flag bypasses exact-window requirement

FACT, reproduced by the worker's host pure-C probe and source references in `coordination/_relay/round-002-frontend-audit.md`: `MacWSAppInputRouteClassify` takes the generic `globalSurface` branch before `MacWSAppInputRouteKindRequiresExactWindow`. Therefore `ConfigureWindow` and `CloseWindow` with `windowID=0` and `MacWSInputFlagGlobalSystemSurface` classify as `GlobalSystemSurface`; the same records without the flag reject with `RejectWindowRequired`. The downstream AppKit lookup returns nil and drops/no-ops, so current impact is fail-safe, but the route contract invariant is false.

The minimal source repair is to handle exact-window kinds before the generic global branch and reject the global flag for them, then add focused assertions for both kinds. Do not apply this while the backend package build owns shared source inputs. The repair must be made after backend handover, followed by focused tests and a fresh package rebuild if the header enters the build.

## Other findings

- TargetProbe record acceptance without an AppInput consumer is pre-existing and labeled FACT/THEORY in the audit; no change in this packet.
- CreateInitialWindow/OpenDocuments classification can be taxonomically Exact while consumers treat sceneID as process/nonce; no behavior change was proven.
- ModifierSnapshot is correctly rejected by the current AppInput tail and routed through its existing proxy.

Decision: R002 is CHANGES_REQUESTED for F1. R001 remains approved with limits for the provider and route implementation. Evidence: `coordination/_relay/round-002-frontend-audit.md`; no device acceptance. Frontend remains idle until backend package ownership is released.
