# Round 003 — frontend result (F1 repair)

Date: 2026-10-18. Scope: fix R002-F1 only; no provider, `AppInputBridge.m`,
package/build input, device or deployment change.

Candidate code HEAD: `4bcf20d` (parent `119e182` is a concurrent backend docs
commit outside this packet).

## Files / SHA-256

| File | Before | After |
| --- | --- | --- |
| `include/macws_app_input_route.h` | `d21f6b421ab5357723c1a3facd9b876b5d40fb7b122a9ca74107902ca1daeb71` | `29d7e17defb694cd3346c5b1868c0784f6c9dda98f8a4cf856d841e5c164440d` |
| `misc/test_app_input_route_contract.py` | `91dbfdedb5cb9c9f5ce979ca97f816c08c0f13ac3603ba7ea773d8b109dce5c2` | `11b5d302abb903b69425c5fd9240830829ede2b3180f1fb20ca2aaf3b39ab483` |

## Change

`MacWSAppInputRouteKindRequiresExactWindow` is now evaluated before the
generic `GlobalSystemSurface` branch:

- `ConfigureWindow`/`CloseWindow` + global flag → `RejectGlobalOwner`
  (window zero or nonzero).
- Same kinds without the flag: window 0 → `RejectWindowRequired`; nonzero →
  `ExactWindow`.
- Pointer/global routes unchanged (`Tap` window 0/44 + flag →
  `GlobalSystemSurface`).
- `RelativePointer` and Dock-only `SystemGesture` branches unchanged.

Regressions added: 8 assertions (both kinds × window 0/73 × flag off/on),
valid global pointer controls, and a source-order assertion pinning the
exact-window branch before the generic global branch.

## Commands / true exits

| Command | Exit |
| --- | --- |
| `python3 -m unittest -v misc.test_app_input_route_contract` (3 tests, OK) | 0 |
| `python3 -m unittest -v misc.test_cydiasubstrate_provider_contract` (2 tests, OK) | 0 |
| `cc … misc/macws_protocol_test.c -lm` + run (`PASS`) | 0 / 0 |
| `/tmp/macws_route_audit` (before/after probe; output in evidence §3) | 0 |
| `git diff --check` | 0 |

Probe after fix: kind 15/16 + flag + window 0 → `-7 (RejectGlobalOwner)`;
without flag → `-6 (RejectWindowRequired)`.

## Limits

- Host pure-C validation only; no device/WindowServer/Dock/AppKit run.
- No package build or deployment. A fresh package rebuild is still required
  when the header enters the build (explicitly out of scope for R003).
- R002 F2/F3/F4 untouched.
- `PerformQuit` + global remains `GlobalSystemSurface`; no observable dispatch
  change was proven and it is outside the authorized F1 scope.
- Backend working-tree files were not read, modified or committed.

## Cleanup

No device markers, jobs or processes. Probe/logs are under `/tmp`; no build
products were produced.

## Git

- Code + test: `4bcf20d`.
- Evidence/result docs: follow-up explicit docs commit (this file and
  `docs/evidence/r003-frontend-route-fix-20261018.md`).

Full detail: `docs/evidence/r003-frontend-route-fix-20261018.md`.

(handover to codex now)
