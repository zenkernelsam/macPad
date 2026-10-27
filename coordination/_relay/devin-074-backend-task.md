# D074 backend task

Local documentation only. Do not contact the device, use SSH or credentials,
execute KRW/TCC/MAC syscalls, deploy/build/restart, alter trustcache, modify
source, or write Git.

Convert the D072/D073 approval packet into a credential-safe operator runbook
for a future explicitly authorized run. Include only placeholders for
`MACWS_DEVICE`, `MACWS_DEVICE_PORT`, and `SSHPASS`; require `sshpass -e`,
pre/post generation witnesses, fail-closed physrw checks, slide-relative
sanitized output, bounded cleanup with `find`, and an explicit stop if a
helper/trustcache mutation would be needed. Mark the runbook as NOT approved
for execution until the user authorizes that exact packet.

Write `docs/evidence/devin-d074-backend-20261019.md` and
`coordination/_relay/devin-074-backend-result.md`, ending exactly with:
`(handover to codex now)`
