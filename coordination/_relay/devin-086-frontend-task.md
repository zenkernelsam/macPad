# D086 frontend task

Offline documentation-only correction. Do not edit source, build, deploy,
restart, contact the device, use credentials, or write Git.

Correct D084's frame-pacing plan using D085 backend findings: restore the
uncached `access(F_OK)` helper while retaining runtime-switch registration for
cleanup, move the arm witness to a first-use sentinel-armed record, require
pre-arm trace removal and generation freshness, re-pin exact current line
numbers, and restate rollback plus audit checks. Do not apply the patch.

Write `docs/evidence/devin-d086-frontend-20261019.md` and
`coordination/_relay/devin-086-frontend-result.md`, ending exactly with:
`(handover to codex now)`
