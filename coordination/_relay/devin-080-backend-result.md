# devin-080 — backend result

Runner v3 written and embedded hash re-verified. Offline only — no
device, credentials, syscalls, deploy, build, trustcache, source, or
Git. Packet remains **NOT APPROVED**; new hash ⇒ new authorization
cycle.

- `docs/evidence/devin-d080-backend-20261019.md`: runner v3 sha256
  `8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`
  (fenced block + trailing newline; re-extraction verified
  byte-identical).
- Sole delta from v2: `DICT_T` now compares `xpc_get_type()` against
  `ctypes.addressof(_xpc_type_dictionary)` — host-verified equal on a
  live dictionary. Everything else unchanged (bounds, double-read,
  sanitization, witnesses, credential handling).
- Residuals recorded: physrw-arm entitlement question (fail-closed),
  cosmetic exception-vs-STOP paths, KV band scope, tolerated unaligned
  version read.

(handover to codex now)
