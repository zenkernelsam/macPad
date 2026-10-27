# D094 backend task

User-authorized, bounded read-only SSH evidence read. Connect only to
`root@192.168.68.63:22`; use the user-authorized password only in transient
`SSHPASS` with `sshpass -e`, never argv/files/logs/Git. No writes, deploy,
restart, trustcache, KRW/TCC, event injection, or source changes.

Run exactly one bounded grep of the current WindowServer error log for
`SENDEROBS-INSTALL|CGXSYNTH-INSTALL`, plus the current WS pid+lstart and
`macws_final_composite.state` for correlation. Redact credentials and
unrelated host details. State whether the D068 sender-observation plan can
run restart-free; absence is only a missing witness, never a negative claim.
Write `docs/evidence/devin-d094-backend-20261019.md` and
`coordination/_relay/devin-094-backend-result.md`, ending exactly with:
`(handover to codex now)`
