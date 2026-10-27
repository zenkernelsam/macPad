# Round 002 — read-only AppInput route audit

This is an independent review-only task while backend R008 owns package/build inputs. Do not edit source, tests, build products, or the backend task. Write findings only to `coordination/_relay/round-002-frontend-audit.md`, including exact commands and exit codes, candidate HEAD, source line references, and FACT/THEORY labels.

Read `coordination/_relay/SYNC.md`, `include/macws_host_protocol.h`, `include/macws_app_input_route.h`, `libmachook/AppInputBridge.m`, and the focused route tests. Enumerate every `MacWSInputKind` against (1) route classification, (2) payload validation, and (3) actual consumer/dispatch. Check that an arbitrary `GlobalSystemSurface` flag cannot widen an unrelated kind, that exact-window kinds reject missing IDs, process-scoped kinds do not invent window ownership, Dock-only gestures remain scoped, and existing per-kind semantics are preserved. Trace any concern to producer/consumer code; do not infer from names or comments alone.

Run only read-only searches/tests that do not touch the backend's active package build. Do not run a package build or device command. If a concrete source defect is found, document the minimal proposed fix and evidence, but do not edit it; return to idle and end with exactly `(handover to codex now)`. Do not make follow-on dispatches.
