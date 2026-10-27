# Frontend R029 — independent saved-candidate invariant audit

R028 auto-upgrade mechanism review accepted. Backend R028 is still Working on isolated candidate, reply capture and cleanup; do not message/interrupt it and do not treat screen summaries as final results.

Read-only independent audit of already completed files under analysis/r028-f11h (list/inventory first). Inspect candidate construction, original-wrapper declared payload vs trailing padding (6916 vs6924), original/converted/candidate hashes and raw attribute records. Evaluate the exact R028 fail-closed criteria: only original intrinsic declaration attrs restored, all non-intrinsic/parameter/call-site attrs retained, body/constants/metadata/calling conventions and intended triple/data layout retained, no reader/check patches. Review whether candidate tooling can corrupt abbreviations/group references/block sizes. Use independent token/record validation, not candidate writer self-check alone. Preserve exact command exits and evidence excerpts.

Do not change shared files or production/converter sources, no build/device/Git. Disposable audit scripts only under analysis/r029-candidate-audit. If files are incomplete or still changing, record stable-file hashes and the precise pending input; do not wait indefinitely or repeatedly poll. Do not claim compiler execution success until final backend artifacts exist. Deliver an actionable accept/reject list for candidate semantics and future productization gates.

Write docs/evidence/r029-f11h-candidate-audit-20261018.md and coordination/_relay/round-029-frontend-result.md. End file and terminal with:
(handover to codex now)
