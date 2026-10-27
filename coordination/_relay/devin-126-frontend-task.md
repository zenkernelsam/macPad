# D126 frontend — attribute the current GUI start exit-1 from saved evidence

Read-only follow-up after D125 handover. Use only the saved, sanitized
2026-10-19 device records and source/runbook. Do not SSH/device-read, start or
restart GUI, edit product source, build, deploy, change trustcache/TCC/KRW, or
write Git.

1. Identify the exact current-generation `macwshostd`/watchdog excerpt that
   records each GUI script exit 1 from
   `coordination/artifacts/current-ipad-macpad-logs-20261019.json` and
   `current-ipad-macpad-startup-logs-20261019.json`. Separate live Oct19
   records from Oct12 archived `guistart.log` / restart logs.
2. Trace the exit-1 producer to the exact shell/helper/source branch and
   determine whether the saved records identify that branch's failing
   prerequisite. Use source plus verbatim runtime lines. If insufficient,
   state the minimum next read-only evidence; do not guess a TCC or
   responsibility link.
3. Give a bounded post-install start acceptance sequence: capture this
   failure boundary before one start, then collect only the precise log/status
   needed to identify the new failing stage. Do not propose loops, longer
   timeouts as a fix, or unrelated broad logs. Preserve reopen freshness
   requirement for visible-window acceptance.
4. Save a concise evidence note and relay result; finish with a separate
   `(handover to codex now)` line.
