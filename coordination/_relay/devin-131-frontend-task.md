# D131 frontend — review D127/D129 and define layer evidence decoder

Read-only follow-up after D129 handover. No device access, source/build/Git,
GUI start/restart, trustcache, TCC/KRW or runtime mutation.

1. Review D127's final package/install evidence and D129's timing design for
   contradictions. Confirm layer 1 is the only accepted D127 layer and that
   `mount_bindfs` absence is an infrastructure blocker, while the restored
   absolute symlink passes the helper contract.
2. Specify the minimal offline decoder/validator for the SENDEROBS fixed
   144-byte record (magic/version/site/invocation/sequence/flags, truncation
   and nested impl->dispatcher->gate order), without inventing a device event.
3. Cross-check the D129 decision threshold and missing current-generation
   thread identity against the existing reopen-freshness and layered-acceptance
   rules. State exactly what a future runtime record would accept or reject.
4. Save one concise review note and relay result; finish with
   `(handover to codex now)`.
