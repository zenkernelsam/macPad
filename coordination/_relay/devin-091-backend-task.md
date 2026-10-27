# D091 backend task

User-authorized, read-only SSH health check only. Connect only to
`root@192.168.68.63` on TCP port 22. The user authorized the password in the
conversation; enter it only into a transient `SSHPASS` environment variable
and use `sshpass -e`, never argv, files, logs, or Git. Do not run any command
that writes, deploys, restarts, changes trustcache, touches KRW/TCC, or alters
the desktop.

Verify authentication and collect only: SSH exit status, WindowServer/tccd
pid+lstart, `macws_final_composite.state`, and the current rootfs identity.
Stop if authentication fails or the endpoint differs. Redact the password and
all sensitive host details from the result. Write
`docs/evidence/devin-d091-backend-20261019.md` and
`coordination/_relay/devin-091-backend-result.md`, ending exactly with:
`(handover to codex now)`
