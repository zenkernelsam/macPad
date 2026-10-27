# D071 frontend task

Read-only follow-up after D070. Work only in the existing macPad checkout.

1. Check the current WindowServer generation for the existing
   `SENDEROBS-INSTALL` evidence using the already-approved read-only path. Do
   not restart WindowServer or inject any event.
2. Audit the saved D037/D041/D070 evidence and source for artificial frame
   waits, service-message handling, and existing reopen-freshness logic.
   State whether a runtime wait measurement is actually available; do not
   infer from source alone.
3. Reconcile the layered acceptance ledger (decode/asset, provider,
   producer/final composite, displayd, Host, visible pixels) and identify only
   missing witnesses. Do not redo accepted provider/F1 or AppInput work.

No device mutation, build, deployment, restart, trustcache change, password,
probe injection, source edit, or Git write. Preserve FACT/THEORY labels and
quote exact log paths/lines. Write:

- `docs/evidence/devin-d071-frontend-20261019.md`
- `coordination/_relay/devin-071-frontend-result.md`

End the result with exactly: `(handover to codex now)`
