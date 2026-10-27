# Frontend R004 — trace XPC interposes that could produce an empty reply

Read reviews/R003-frontend-20261018.md: F1 accepted as offline repair only; header changes require later combined-tree build. Backend R002 is currently tracing device TCC empty replies. Do not message or interrupt backend.

Independently audit current source for XPC handling between Ventura TCC.framework and chroot tccd. Inventory active DYLD_INTERPOSE/rebinding/hook definitions for connection creation, event-handler installation, reply creation/send, dictionary reads/writes and entitlement/audit-token adaptation. Trace any generic server event handler or compatibility fallback that constructs/sends an empty dictionary, substitutes a callback, drops dispatch, or suppresses the real handler. Use exact source line references and original-call chain. Determine process/role/env gates and whether this can affect tccd, without assuming source presence proves live execution.

Compare the observed seven-key AccessRequest/empty reply with source contracts and share-package role adaptations. If you find a concrete source mechanism, write its exact path, activation preconditions, evidence that would verify/refute it in the running tccd, and a minimal upstream correction proposal. Otherwise report the covered paths and remaining uncertainty. Do not add policy bypasses, fabricate requests/replies, modify TCC databases or infer privilege denial from empty fields.

READ ONLY source audit. Write only coordination/_relay/round-004-frontend-result.md and your own dated evidence note. No source/tests/build edits, package build, device commands, dependencies, Git commit/reset/stash/push. Backend owns device observations; coordinator will consolidate both results after handover. Label source-confirmed versus THEORY/runtime explicitly. Preserve all existing WIP.

Finish a bounded report with inspected file hashes, commands/exits and next discriminating experiment. Last line exactly:
(handover to codex now)
