# devin-095 — backend result

Offline audit of R005/R006/R014/R024/R031/R032 + D033 park instruction
complete. No device contact, execution, edits, trustcache, or Git.
Evidence: `docs/evidence/devin-d095-backend-20261019.md`.

- **Park stands**: D033 ("R031 never compiler-tested; park it") +
  worker-switch ("visible desktop before compiler integration")
  remain binding; replay activates only on a renewed named grant.
- **Hash-bound checklist verified**: candidate request
  `75663758…da1e` (7,184 B), record `5d805ec5…`, MTLB `7b71a647…`,
  control `ffa7daa5…`; endpoint binaries/wrappers/diag dylib + client/
  redirect CDHashes all enumerated; local artifacts re-hashed intact.
- **Exact scope**: restore owned tree → verify trustcache → control
  run (expect attr-71) → candidate run → conditional no-hook confirm;
  success = real object/specialized function only, not just a reply.
- **Rollback**: mv tree back, zero owned procs, find-diff, markers
  absent, compiler count to baseline, crashes preserved as evidence,
  trustcache entries stay inert-documented.
- **R005/IconServices link needs**: same-module proof (production
  request hash vs `5d805ec5…`), separate production-side MTLB A/B as
  its own grant, no-hook elimination before any acceptance claim.

(handover to codex now)
