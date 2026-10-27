# devin-082 — backend result

User-facing authorization text drafted for runner v3. Offline only —
no execution, device, credentials, syscalls, deploy, source, or Git.

- `docs/evidence/devin-d082-backend-20261019.md`: the verbatim
  authorization block for `macws-policy-snapshot-v3`, pinned to sha256
  `8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`.
- Covers: one bounded read-only `mac_policy_list` snapshot; declared
  read set; sanitized-output contract; `read -s SSHPASS`/`sshpass -e`
  credential handling with placeholders; pre/post pid+lstart +
  `state=ready` + baseline `find` diff witnesses; all STOP terminals;
  `physrw-unarmed` → `missing-authorization` terminal.
- Explicitly excluded by the grant: helper binaries, trustcache
  mutation, deployment, restart, second/modified runs, reads outside
  the declared set — any deviation voids admissibility.
- Status: ready to present; still requires the user's named grant.

(handover to codex now)
