# D128 frontend — independent audit of IconServices crash identity and trust

Read-only evidence audit after D126 handover. Use only saved artifacts and
source in the workspace. No SSH/device access, build, source edit, trustcache,
TCC/KRW, GUI start/restart or Git write.

1. Reconcile the verbatim R005 crash excerpt, D033 crash inventory, R013
   IconServices report, D071 audit, D094 layered acceptance and D095 F11H
   linkage audit. Quote the actual crashing process path, architecture/UUID,
   process user/parent, crash assertion/stack, loaded `libmachook` identity,
   and dependency-trust evidence. Distinguish agent identity from sibling
   `iconservicesd` identity.
2. Verify directory facts from saved evidence: `HOME`, `TMPDIR`, absent/present
   agent cache, `iconservicesd` store path/owner/mode, and stability evidence.
   Do not turn directory observations into a crash cause without runtime
   evidence.
3. Audit the causal boundary: whether the saved crash proves an MPS F11H
   compiler failure, whether R024 attr-71 compiler rejection is same-module
   evidence, and whether R031 retarget has been replayed in the exact service.
   Preserve D033's park and D095's no-same-module limit; do not claim the
   candidate fixes production.
4. Compare the saved MacOS IconServices crash with the user's reported iPad
   reboot/panic and the 2026-10-19 read-only device bundle. If no panic report
   exists in saved evidence, say that explicitly and do not attribute the
   reboot to IconServices.
5. Produce one concise evidence note with FACT/THEORY/MISSING labels and the
   minimum next artifact for any unresolved claim. End with a separate
   `(handover to codex now)` line.
