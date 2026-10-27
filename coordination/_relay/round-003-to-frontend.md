# Frontend R003 — exact-window regression repair

Read SYNC.md and coordination/reviews/R002-frontend-route-audit-20261018.md. Fix the demonstrated F1 in include/macws_app_input_route.h: ConfigureWindow/CloseWindow require nonzero exact window even with GlobalSystemSurface; reject that incompatible flag without changing valid pointer/global routes, RelativePointer or Dock SystemGesture. Add behavioral regressions for both kinds with window zero/nonzero and flag on/off.

Only edit that header, misc/test_app_input_route_contract.py and your own evidence/result documents. No provider/AppInputBridge change, package build, device action or deployment. Run focused route/provider and protocol checks, preserve real exits, hashes and limits. You own Git writes for explicit task paths only; backend concurrently performs read-only TCC analysis. No stash/reset/push or unrelated WIP commits.

Write coordination/_relay/round-003-frontend-result.md and dated evidence with candidate HEAD/fingerprints. No follow-on task. Last line exactly:
(handover to codex now)
