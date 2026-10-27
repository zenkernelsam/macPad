# D123 frontend — audit D121 responsibility-slot claims and evidence levels

Read-only audit of `docs/evidence/devin-d121-frontend-20261019.md`,
`coordination/artifacts/probe-inventory-r003*.json`, R013/R025/R009, and D117–D121.
No device/SSH, production edits, build, Git, TCC setter, KRW, kernel snapshot,
or GUI start/retry.

1. Verify which r003 artifact(s) and exact command/output contain the cited
   stub `va=`/`fn=` pairs. Record dates, exits, loaded image UUIDs, and whether
   the runtime process was WindowServer, a probe, or another chroot process.
2. Separate these claims: (a) cache map is RW/shared VA space; (b) probe's own
   authenticated stubs resolve to named images; (c) target libquarantine slot
   `0x1dfef8f88` is resolved and controls the responsibility/TCC result. State
   which are runtime facts, static facts, calculations, theories, or still
   missing. Probe-owned slots must not substitute for a read/trace of the
   target slot.
3. Audit D121's “single decisive live 8 bytes” language: specify whether raw
   signed bytes alone establish the authenticated branch target, what PAC
   stripping/lookup can establish, and what additional witness (e.g. actual
   stub branch/call trace or fixup metadata) is needed to confirm execution and
   the upstream provider's behavior.
4. Reconcile the `__auth_got` zero-hole/file-offset correction against the
   actual extraction size/section metadata; do not conflate file offsets,
   unslid VMAs, and cache offsets.
5. Return one concise corrected evidence note with exact paths/hashes/commands
   and the smallest safe next artifact. In particular, do not link the
   unrelated current GUI-start exit 1 to TCC without direct evidence. Stop with
   a separate `(handover to codex now)` line.
