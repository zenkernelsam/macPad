# devin-075 — backend result

Audit of the D074 runbook complete; offline only. The packet stays
**NOT APPROVED** — ten concrete corrections recorded in
`docs/evidence/devin-d075-backend-20261019.md`:

- C1 blocking: `run_snapshot.py` must be embedded inline + sha256-pinned
  or authorization can't bind to exact bytes.
- C2: identity-verification reads (magic + version string) are outside
  the task's literal "read only mac_policy_list" set — must be declared
  as the authorized verification read set, or the run violates its own
  contract on the first check.
- C3: init/sysinfo returns+types must gate before key reads.
- C4: post-witness must compare `lstart`, not pid alone.
- C5: drop `$SSH` command-in-variable; spell the full command, add
  `ConnectTimeout`.
- C6: `SSHPASS` via `read -s`, never an `export` line in history.
- C7: cleanup assertion should diff a pre/post `find` baseline, not
  pattern-match names this run never creates.
- C8: seq-delta semantics specified (health = same pid+lstart + ready;
  delta informational).
- C9: pin `/var/jb/usr/bin/python3`.
- C10: add `UserKnownHostsFile=/dev/null` to avoid interactive prompts.

All mechanical; AGENTS.md consistency otherwise holds (placeholders,
no creds, honest limits).

(handover to codex now)
