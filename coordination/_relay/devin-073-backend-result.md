# devin-073 — backend result

Go/no-go memo written, read-only. No device, SSH, credentials,
syscalls, deploy, build, trustcache, source, or Git action.

- **Verdict: CONDITIONAL GO.** The D072 packet meets AGENTS.md and the
  share-lessons checklist as specified, provided the physrw arm check
  runs inside the authorized run as a fail-closed precondition — not
  as a separate probe — and provided it terminates "missing input"
  if primitives do not arm (the helper fallback would need a
  trustcache add, which is out of contract).
- **Already closed**: kernel identity (img4↔device byte-identical),
  static Quarantine-registration impossibility, the three-arm
  `mac_syscall` model, namespace exclusion (R004), endpoint/route map,
  desktop health channel.
- **Needs authorization**: only the single bounded
  `mac_policy_list` snapshot — plus its exact credential/arm/cleanup
  clauses. Frontend sender-observation is a separate frontend packet.
- **Locally answerable residuals**: per-policy `ops+0x388` presence is
  offline-determinable only with a PAC-aware fixup decoder (larger RE
  task); what Sandbox's precheck *returns* for name="Quarantine" is a
  runtime property no local read settles; the dynamic-name edge is
  cheaper to close with the snapshot than with the decoder.
- **Evidence/cleanup contract** restated verbatim: SSHPASS env, zero
  trustcache mutation, pre/post generation witnesses, slide-relative
  output only, verbatim stop conditions, `find`-based cleanup (no
  globs), no raw KASLR in saved transcripts.

Evidence: `docs/evidence/devin-d073-backend-20261019.md`

(handover to codex now)
