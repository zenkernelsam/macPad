# devin-076 — backend result

Runbook v2 written with all ten D075 corrections applied. Offline
documentation only — no device, SSH, credentials, syscalls, deploy,
build, trustcache, source, or Git action. Packet remains **NOT
APPROVED** until the user authorizes it by name.

- `docs/evidence/devin-d076-backend-20261019.md` — hash manifest binds
  authorization to exact bytes: runner sha256
  `96e47c3b530684d8af1243c19faa34bce351a4d417c868174a26ba705b0f5da5`
  (fenced block + trailing newline, verified the embedded block is
  byte-identical to the hashed file) plus all verified constants.
- Read set now declared: identity reads (8 B magic + 112 B version)
  enumerated alongside the list header/entries — no uncontracted reads.
- Every return/type check gated (`init-prims`, `physrw-unarmed`,
  `sysinfo`, `sysinfo-type`, `base-slide`, `magic`, `version`,
  `torn-header`, `bad-header`, per-entry stops).
- Pre/post witnesses compare pid+**lstart**; cleanup is a baseline
  `find` diff, not pattern matching; seq-delta semantics defined.
- Credentials via `read -s SSHPASS`; full quoted SSH commands with
  `ConnectTimeout=8` and `UserKnownHostsFile=/dev/null`;
  `/var/jb/usr/bin/python3` pinned; zero staged files on device.

(handover to codex now)
